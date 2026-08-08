-- Helper: resolve a devkitPro tool by name
local function dkp_tool(name)
    local DEVKITPRO = os.getenv("DEVKITPRO") or "/opt/devkitpro"
    local full = path.join(DEVKITPRO, "tools", "bin", name)
    if os.isfile(full) then return full end
    -- fall back to PATH (distro packages put tools in /usr/bin)
    return name
end

-- Rule: switch
rule("switch")

    -- Switch U V8.1: add the official devkitPro FFmpeg port only to the
    -- SwitchU menu target. The daemon and the rest of the project are untouched.
    on_load(function(target)
        if target:name() ~= "SwitchU" then return end

        local DEVKITPRO = os.getenv("DEVKITPRO") or "/opt/devkitpro"
        local portlibs = path.join(DEVKITPRO, "portlibs", "switch")
        local pkg_config = path.join(portlibs, "bin", "aarch64-none-elf-pkg-config")
        local avcodec = path.join(portlibs, "lib", "libavcodec.a")

        if not os.isfile(pkg_config) or not os.isfile(avcodec) then
            raise("SwitchU V8.1 MP4 requires the devkitPro package 'switch-ffmpeg'. Install it, then re-run xmake.")
        end

        target:add("defines", "SWITCHU_V81_FFMPEG")
        target:add("includedirs", path.join(portlibs, "include"))

        -- Ask the Switch pkg-config wrapper for the complete static link chain.
        -- This keeps FFmpeg's transitive dependencies in the correct order.
        local libflags = os.iorunv(pkg_config, {
            "--libs", "--static",
            "libavformat", "libavcodec", "libswscale", "libavutil"
        })

        local linkdirs = {}
        local links = {}
        local ldflags = {}
        for token in libflags:gmatch("%S+") do
            if token:startswith("-L") then
                table.insert(linkdirs, token:sub(3))
            elseif token:startswith("-l") then
                table.insert(links, token:sub(3))
            else
                table.insert(ldflags, token)
            end
        end

        for _, dir in ipairs(linkdirs) do
            target:add("linkdirs", dir)
        end
        for _, link in ipairs(links) do
            target:add("links", link)
        end
        for _, flag in ipairs(ldflags) do
            target:add("ldflags", flag, {force = true})
        end
    end)

    -- Shaders: compile GLSL -> DKSH via uam before linking 
    before_build(function(target)
        local shaderdir = path.join(os.projectdir(), "shaders")
        if not os.isdir(shaderdir) then return end

        local romfs = target:values("switch.romfs")
        if not romfs then return end
        romfs = path.absolute(romfs, os.projectdir())
        local outdir = path.join(romfs, "shaders")
        os.mkdir(outdir)

        local uam = dkp_tool("uam")
        for _, src in ipairs(os.files(path.join(shaderdir, "*.glsl"))) do
            local base = path.basename(src)
            local out  = path.join(outdir, base .. ".dksh")

            -- skip if up-to-date
            if os.isfile(out) and os.mtime(out) >= os.mtime(src) then
                goto continue
            end

            local stage
            if base:endswith("_vsh") then stage = "vert"
            elseif base:endswith("_fsh") then stage = "frag"
            else
                raise("shader filename must end with _vsh or _fsh: " .. base)
            end

            cprint("${color.build.target}compiling shader${clear} %s", path.filename(src))
            os.vrunv(uam, {"-s", stage, "-o", out, src})

            ::continue::
        end
    end)

    -- After build: produce NRO or NSP from the ELF 
    after_build(function(target)
        if target:kind() ~= "binary" then return end

        local format  = target:values("switch.format") or "nro"
        local name    = target:values("switch.name")    or target:name()
        local author  = target:values("switch.author")  or "unknown"
        local version = target:values("switch.version") or "1.0.0"
        local elf     = target:targetfile()
        local outdir  = target:targetdir()

        if format == "nro" then
            -- NRO 
            local nacpfile = path.join(outdir, name .. ".nacp")
            cprint("${color.build.target}generating${clear} %s", path.filename(nacpfile))
            os.vrunv(dkp_tool("nacptool"),
                     {"--create", name, author, version, nacpfile})

            local nrofile = path.join(outdir, name .. ".nro")
            local args    = {elf, nrofile, "--nacp=" .. nacpfile}

            -- icon
            local icon = target:values("switch.icon")
            if icon and icon ~= "" and os.isfile(icon) then
                table.insert(args, "--icon=" .. icon)
            else
                local DEVKITPRO = os.getenv("DEVKITPRO") or "/opt/devkitpro"
                local deficon   = path.join(DEVKITPRO, "libnx", "default_icon.jpg")
                if os.isfile(deficon) then
                    table.insert(args, "--icon=" .. deficon)
                end
            end

            -- embedded romfs
            local romfs = target:values("switch.romfs")
            if romfs and romfs ~= "" then
                romfs = path.absolute(romfs, os.projectdir())
                table.insert(args, "--romfsdir=" .. romfs)
            end

            cprint("${color.build.target}generating${clear} %s", path.filename(nrofile))
            os.vrunv(dkp_tool("elf2nro"), args)

        elseif format == "nsp" then
            -- NSP 
            local exefsdir = path.join(outdir, "exefs", name)
            os.mkdir(exefsdir)

            -- ELF → NSO
            cprint("${color.build.target}generating${clear} main (NSO)")
            os.vrunv(dkp_tool("elf2nso"), {elf, path.join(exefsdir, "main")})

            -- JSON → NPDM
            local jsoncfg = target:values("switch.json")
            if jsoncfg then
                jsoncfg = path.absolute(jsoncfg, os.projectdir())
                cprint("${color.build.target}generating${clear} main.npdm")
                os.vrunv(dkp_tool("npdmtool"), {jsoncfg, path.join(exefsdir, "main.npdm")})
            end

            -- PFS0 → NSP
            local nspfile = path.join(outdir, name .. ".nsp")
            cprint("${color.build.target}generating${clear} %s", path.filename(nspfile))
            os.vrunv(dkp_tool("build_pfs0"), {exefsdir, nspfile})

        end
    end)

    -- Install: create SD-card directory layout
    on_install(function(target)
        local installdir = target:installdir()
        local format     = target:values("switch.format") or "nro"
        local name       = target:values("switch.name")   or target:name()
        local romfs      = target:values("switch.romfs")
        if romfs then romfs = path.absolute(romfs, os.projectdir()) end
        local outdir     = target:targetdir()

        if format == "nro" then
            -- sdmc:/switch/<name>/<name>.nro
            local dest = path.join(installdir, "switch", name)
            os.mkdir(dest)
            os.cp(path.join(outdir, name .. ".nro"), path.join(dest, name .. ".nro"))
            cprint("${bright green}installed${clear} NRO → %s", dest)

        else
            if format == "nsp" then
                local install_contents = target:values("switch.install_contents")
                if install_contents == nil then
                    install_contents = true
                end

                if install_contents then
                    -- NSP as ExeFS override via Atmosphère LayeredFS.
                    local tid         = target:values("switch.tid") or "0000000000000000"
                    local contentsdir = path.join(installdir, "atmosphere", "contents", tid)
                    os.mkdir(contentsdir)
                    os.cp(path.join(outdir, name .. ".nsp"), path.join(contentsdir, "exefs.nsp"))
                    cprint("${bright green}installed${clear} NSP → %s", contentsdir)
                end

                local raw_exefs_dir = target:values("switch.raw_exefs_dir")
                if raw_exefs_dir and raw_exefs_dir ~= "" then
                    local raw_dest = path.join(installdir, raw_exefs_dir)
                    os.mkdir(raw_dest)
                    os.cp(path.join(outdir, "exefs", name, "main"), path.join(raw_dest, "main"))
                    os.cp(path.join(outdir, "exefs", name, "main.npdm"), path.join(raw_dest, "main.npdm"))
                    cprint("${bright green}installed${clear} raw ExeFS → %s", raw_dest)
                end
            end

            -- SD assets: sdmc:/switch/<assets_name>/
            if romfs and os.isdir(romfs) then
                local assets_name = target:values("switch.assets_dir") or name
                local assetsdir   = path.join(installdir, "switch", assets_name)
                for _, sub in ipairs({"fonts", "sounds", "icons", "i18n", "shaders", "espeak-ng-data"}) do
                    local src = path.join(romfs, sub)
                    if sub == "espeak-ng-data" then
                        local generated = path.join(os.projectdir(), "build/espeak-ng-native/espeak-ng-data")
                        local vendored = path.join(os.projectdir(), "lib/espeak-ng/espeak-ng-data")
                        if os.isdir(generated) then
                            src = generated
                        elseif os.isdir(vendored) then
                            src = vendored
                        end
                    end
                    if os.isdir(src) then
                        local dst = path.join(assetsdir, sub)
                        os.mkdir(dst)
                        os.cp(path.join(src, "*"), dst)
                    end
                end

                cprint("${bright green}installed${clear} assets → %s", assetsdir)
            end
        end
    end)

    -- Run: launch NRO via emulator or nxlink
    on_run(function(target)
        local format = target:values("switch.format") or "nro"
        if format ~= "nro" then
            raise("xmake run is only supported for homebrew (.nro) builds. Use: xmake f --homebrew=y")
        end

        import("core.base.option")
        import("lib.detect.find_tool")

        local outdir = target:targetdir()
        local name   = target:values("switch.name") or target:name()
        local nro    = path.join(outdir, name .. ".nro")

        if not os.isfile(nro) then
            raise("%s not found – run xmake build first", nro)
        end

        local args = table.wrap(option.get("arguments"))
        local ip_addr
        for _, arg in ipairs(args) do
            if arg:startswith("--nx=") then
                ip_addr = arg:sub(6)
                break
            end
        end

        if ip_addr then
            cprint("${color.build.target}nxlink${clear} → %s", ip_addr)
            os.execv("nxlink", {"-a", ip_addr, "-s", nro})
        else
            local emu = "eden"
            
            if emu then
                cprint("${color.build.target}launching${clear} %s in %s", path.filename(nro), emu)
                os.execv(emu, {nro})
            else
                cprint("${bright yellow}no emulator found – copy %s to your Switch${clear}", nro)
            end
        end
    end)

rule_end()
