#pragma once
#include "Texture.hpp"
#include "Font.hpp"
#include <vector>
namespace nxui {
struct Renderer {
 bool record=true;
 GpuDevice device; std::vector<std::vector<double>> commands;
 GpuDevice& gpu(){return device;}
 void pushClipRect(const Rect&r){if(record)commands.push_back({0,r.x,r.y,r.width,r.height});}
 void popClipRect(){if(record)commands.push_back({1});}
 void drawTexture(const Texture*,const Rect&r,const Color&c=Color::white()) {if(record)commands.push_back({2,r.x,r.y,r.width,r.height,c.r,c.g,c.b,c.a});}
 void drawGradientRect(const Rect&r,const Color&a,const Color&b){if(record)commands.push_back({3,r.x,r.y,r.width,r.height,a.r,a.g,a.b,a.a,b.r,b.g,b.b,b.a});}
 void drawRect(const Rect&r,const Color&c){if(record)commands.push_back({4,r.x,r.y,r.width,r.height,c.r,c.g,c.b,c.a});}
 void drawRoundedRect(const Rect&r,const Color&c,float){drawRect(r,c);}
 void drawTriangle(const Vec2&a,const Vec2&b,const Vec2&c,const Color&color){if(record)commands.push_back({5,a.x,a.y,b.x,b.y,c.x,c.y,color.r,color.g,color.b,color.a});}
 void drawTexturedTriangle(int slot,const Vec2&a,const Vec2&ua,const Vec2&b,const Vec2&ub,const Vec2&c,const Vec2&uc,const Color&t){if(record)commands.push_back({6,double(slot),a.x,a.y,ua.x,ua.y,b.x,b.y,ub.x,ub.y,c.x,c.y,uc.x,uc.y,t.r,t.g,t.b,t.a});}
 void drawCircle(const Vec2&c,float radius,const Color&color,int segments=24){if(record)commands.push_back({7,c.x,c.y,radius,color.r,color.g,color.b,color.a,double(segments)});}
 void drawLine(const Vec2&a,const Vec2&b,const Color&c,float w=1){if(record)commands.push_back({8,a.x,a.y,b.x,b.y,c.r,c.g,c.b,c.a,w});}
 void drawColoredTriangle(const Vec2&a,const Color&ca,const Vec2&b,const Color&cb,const Vec2&c,const Color&cc){if(record)commands.push_back({9,a.x,a.y,ca.r,ca.g,ca.b,ca.a,b.x,b.y,cb.r,cb.g,cb.b,cb.a,c.x,c.y,cc.r,cc.g,cc.b,cc.a});}
 void drawText(const std::string&,const Vec2&,Font*,const Color&,float){}
};
}
