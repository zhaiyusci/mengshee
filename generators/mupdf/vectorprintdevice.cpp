// SPDX-License-Identifier: AGPL-3.0-or-later
#include "vectorprintdevice.h"
#include <QImage>
#include <QPainter>
#include <QPainterPathStroker>
#include <QRegion>
#include <algorithm>
#include <vector>

namespace {
struct State {
    QPainter *painter;
    QRect page;
    QRegion *fallback;
    bool skip = false;
    std::vector<bool> clips, groups, tiles;
};
struct Device { fz_device base; State *state; };
State &state(fz_device *d) { return *reinterpret_cast<Device *>(d)->state; }
QTransform matrix(fz_matrix m) { return {m.a,m.b,m.c,m.d,m.e,m.f}; }
QRectF rect(fz_rect r) { return {r.x0,r.y0,r.x1-r.x0,r.y1-r.y0}; }
void fallback(fz_device *d, fz_rect bounds)
{
    auto &s=state(d);
    const QRectF page(s.page);
    if (!fz_is_valid_rect(bounds) || fz_is_infinite_rect(bounds)) bounds={float(page.left()),float(page.top()),float(page.right()),float(page.bottom())};
    QRect area=rect(bounds).adjusted(-2,-2,2,2).intersected(page).toAlignedRect();
    if (s.painter->hasClipping()) area &= s.painter->clipBoundingRect().toAlignedRect();
    *s.fallback += area;
}
QPainterPath path(fz_context *ctx, const fz_path *source)
{
    QPainterPath p;
    const fz_path_walker walker {
        [](fz_context *,void *p,float x,float y){static_cast<QPainterPath *>(p)->moveTo(x,y);},
        [](fz_context *,void *p,float x,float y){static_cast<QPainterPath *>(p)->lineTo(x,y);},
        [](fz_context *,void *p,float x1,float y1,float x2,float y2,float x3,float y3){static_cast<QPainterPath *>(p)->cubicTo(x1,y1,x2,y2,x3,y3);},
        [](fz_context *,void *p){static_cast<QPainterPath *>(p)->closeSubpath();}
    };
    fz_walk_path(ctx,source,&walker,&p);
    return p;
}
QColor color(fz_context *ctx,fz_colorspace *space,const float *values,float alpha,fz_color_params params)
{
    float rgb[3]={};
    if(space) fz_convert_color(ctx,space,values,fz_device_rgb(ctx),rgb,nullptr,params);
    return QColor::fromRgbF(std::clamp(rgb[0],0.f,1.f),std::clamp(rgb[1],0.f,1.f),std::clamp(rgb[2],0.f,1.f),std::clamp(alpha,0.f,1.f));
}
QPen pen(const fz_stroke_state *stroke,const QColor &c)
{
    QPen p(c,stroke->linewidth);
    p.setCapStyle(stroke->start_cap==FZ_LINECAP_ROUND?Qt::RoundCap:stroke->start_cap==FZ_LINECAP_SQUARE?Qt::SquareCap:Qt::FlatCap);
    p.setJoinStyle(stroke->linejoin==FZ_LINEJOIN_ROUND?Qt::RoundJoin:stroke->linejoin==FZ_LINEJOIN_BEVEL?Qt::BevelJoin:Qt::MiterJoin);
    p.setMiterLimit(stroke->miterlimit);
    if(stroke->dash_len) {
        QList<qreal> dash;
        const double unit=stroke->linewidth>0?stroke->linewidth:1;
        for(int i=0;i<stroke->dash_len;++i) dash.append(stroke->dash_list[i]/unit);
        if(dash.size()%2) dash+=dash;
        p.setDashPattern(dash); p.setDashOffset(stroke->dash_phase/unit);
    }
    return p;
}
QPainterPath stroked(const QPainterPath &p,const fz_stroke_state *stroke)
{
    QPainterPathStroker stroker;
    const auto style=pen(stroke,Qt::black);
    stroker.setWidth(style.widthF()>0?style.widthF():1);
    stroker.setCapStyle(style.capStyle()); stroker.setJoinStyle(style.joinStyle()); stroker.setMiterLimit(style.miterLimit());
    if(stroke->dash_len) { stroker.setDashPattern(style.dashPattern()); stroker.setDashOffset(style.dashOffset()); }
    return stroker.createStroke(p);
}
void draw(fz_context *ctx,fz_device *d,const QPainterPath &p,const fz_stroke_state *stroke,fz_matrix ctm,fz_colorspace *cs,const float *values,float alpha,fz_color_params params)
{
    auto &s=state(d);
    if(s.skip) return;
    // Avoid letting a printer engine flatten an entire page to emulate alpha.
    if(alpha != 1 || params.op || (stroke && (stroke->start_cap != stroke->end_cap || stroke->start_cap == FZ_LINECAP_TRIANGLE))) {
        const auto bounds=matrix(ctm).map(stroke?stroked(p,stroke):p).boundingRect();
        fallback(d,{float(bounds.left()),float(bounds.top()),float(bounds.right()),float(bounds.bottom())});
        return;
    }
    const auto c=color(ctx,cs,values,alpha,params);
    s.painter->save();
    s.painter->setWorldTransform(matrix(ctm));
    s.painter->setPen(stroke?pen(stroke,c):QPen(Qt::NoPen));
    s.painter->setBrush(stroke?QBrush(Qt::NoBrush):QBrush(c));
    s.painter->drawPath(p);
    s.painter->restore();
}
void clip(fz_device *d,const QPainterPath &p,fz_matrix ctm)
{
    auto &s=state(d); s.clips.push_back(s.skip); s.painter->save();
    if(!s.skip) s.painter->setClipPath(matrix(ctm).map(p),Qt::IntersectClip);
}
QPainterPath textPath(fz_context *ctx,const fz_text *text,bool &complete)
{
    QPainterPath result; result.setFillRule(Qt::WindingFill); complete=true;
    fz_path *outline=nullptr;
    fz_var(outline);
    fz_try(ctx) {
        for(auto *span=text->head;span;span=span->next) for(int i=0;i<span->len;++i) {
            const auto &item=span->items[i]; if(item.gid<0) continue;
            auto trm=span->trm; trm.e=item.x; trm.f=item.y;
            outline=fz_outline_glyph(ctx,span->font,item.gid,trm);
            if(outline) result.addPath(path(ctx,outline));
            else complete=false;
            fz_drop_path(ctx,outline); outline=nullptr;
        }
    }
    fz_always(ctx) { fz_drop_path(ctx,outline); }
    fz_catch(ctx) { fz_rethrow(ctx); }
    return result;
}
void fillPath(fz_context *ctx,fz_device *d,const fz_path *p,int evenOdd,fz_matrix ctm,fz_colorspace *cs,const float *c,float a,fz_color_params cp)
{
    if(state(d).skip) return;
    auto q=path(ctx,p); q.setFillRule(evenOdd?Qt::OddEvenFill:Qt::WindingFill); draw(ctx,d,q,nullptr,ctm,cs,c,a,cp);
}
void strokePath(fz_context *ctx,fz_device *d,const fz_path *p,const fz_stroke_state *s,fz_matrix ctm,fz_colorspace *cs,const float *c,float a,fz_color_params cp)
{
    if(!state(d).skip) draw(ctx,d,path(ctx,p),s,ctm,cs,c,a,cp);
}
void clipPath(fz_context *ctx,fz_device *d,const fz_path *p,int evenOdd,fz_matrix ctm,fz_rect)
{ auto q=path(ctx,p); q.setFillRule(evenOdd?Qt::OddEvenFill:Qt::WindingFill); clip(d,q,ctm); }
void clipStroke(fz_context *ctx,fz_device *d,const fz_path *p,const fz_stroke_state *s,fz_matrix ctm,fz_rect)
{ clip(d,stroked(path(ctx,p),s),ctm); }
void fillText(fz_context *ctx,fz_device *d,const fz_text *t,fz_matrix ctm,fz_colorspace *cs,const float *c,float a,fz_color_params cp)
{
    if(state(d).skip) return;
    bool complete; const auto p=textPath(ctx,t,complete);
    if(!complete) fallback(d,fz_bound_text(ctx,t,nullptr,ctm));
    else draw(ctx,d,p,nullptr,ctm,cs,c,a,cp);
}
void strokeText(fz_context *ctx,fz_device *d,const fz_text *t,const fz_stroke_state *s,fz_matrix ctm,fz_colorspace *cs,const float *c,float a,fz_color_params cp)
{
    if(state(d).skip) return;
    bool complete; const auto p=textPath(ctx,t,complete);
    if(!complete) fallback(d,fz_bound_text(ctx,t,s,ctm)); else draw(ctx,d,p,s,ctm,cs,c,a,cp);
}
void clipText(fz_context *ctx,fz_device *d,const fz_text *t,fz_matrix ctm,fz_rect)
{
    bool complete; const auto p=textPath(ctx,t,complete);
    if(!complete && !state(d).skip) fallback(d,fz_bound_text(ctx,t,nullptr,ctm));
    clip(d,p,ctm);
    if(!complete) state(d).skip=true;
}
void clipStrokeText(fz_context *ctx,fz_device *d,const fz_text *t,const fz_stroke_state *s,fz_matrix ctm,fz_rect)
{
    bool complete; const auto p=textPath(ctx,t,complete);
    if(!complete && !state(d).skip) fallback(d,fz_bound_text(ctx,t,s,ctm));
    clip(d,stroked(p,s),ctm);
    if(!complete) state(d).skip=true;
}
void popClip(fz_context *,fz_device *d)
{ auto &s=state(d); if(!s.clips.empty()) { s.painter->restore(); s.skip=s.clips.back(); s.clips.pop_back(); } }
void fillImage(fz_context *ctx,fz_device *d,fz_image *image,fz_matrix ctm,float alpha,fz_color_params params)
{
    if(state(d).skip) return;
    if(image->mask || alpha != 1 || params.op) { fallback(d,fz_transform_rect(fz_unit_rect,ctm)); return; }
    fz_pixmap *pix=nullptr,*rgb=nullptr;
    fz_var(pix); fz_var(rgb);
    fz_try(ctx) {
        pix=fz_get_unscaled_pixmap_from_image(ctx,image);
        rgb=fz_convert_pixmap(ctx,pix,fz_device_rgb(ctx),nullptr,nullptr,params,1);
        QImage q(fz_pixmap_samples(ctx,rgb),fz_pixmap_width(ctx,rgb),fz_pixmap_height(ctx,rgb),fz_pixmap_stride(ctx,rgb),
                 fz_pixmap_alpha(ctx,rgb)?QImage::Format_RGBA8888_Premultiplied:QImage::Format_RGB888);
        auto *p=state(d).painter;
        p->save(); p->setWorldTransform(matrix(ctm)); p->setOpacity(alpha);
        p->drawImage(QRectF(0,0,1,1),q); p->restore();
    }
    fz_always(ctx) { fz_drop_pixmap(ctx,rgb); fz_drop_pixmap(ctx,pix); }
    fz_catch(ctx) { fz_rethrow(ctx); }
}
void imageMask(fz_context *,fz_device *d,fz_image *,fz_matrix ctm,fz_colorspace *,const float *,float,fz_color_params)
{ if(!state(d).skip) fallback(d,fz_transform_rect(fz_unit_rect,ctm)); }
void clipImage(fz_context *,fz_device *d,fz_image *,fz_matrix ctm,fz_rect)
{
    auto &s=state(d); s.clips.push_back(s.skip); s.painter->save();
    if(!s.skip) fallback(d,fz_transform_rect(fz_unit_rect,ctm)); s.skip=true;
}
void shade(fz_context *ctx,fz_device *d,fz_shade *sh,fz_matrix ctm,float,fz_color_params)
{ if(!state(d).skip) fallback(d,fz_bound_shade(ctx,sh,ctm)); }
void beginMask(fz_context *,fz_device *d,fz_rect area,int,fz_colorspace *,const float *,fz_color_params)
{
    auto &s=state(d); s.clips.push_back(s.skip); s.painter->save();
    if(!s.skip) fallback(d,area); s.skip=true;
}
void beginGroup(fz_context *,fz_device *d,fz_rect area,fz_colorspace *,int,int knockout,int blend,float alpha)
{
    auto &s=state(d); s.groups.push_back(s.skip);
    if(!s.skip && (knockout || blend!=FZ_BLEND_NORMAL || alpha!=1)) { fallback(d,area); s.skip=true; }
}
void endGroup(fz_context *,fz_device *d)
{ auto &s=state(d); if(!s.groups.empty()) { s.skip=s.groups.back(); s.groups.pop_back(); } }
int beginTile(fz_context *,fz_device *d,fz_rect area,fz_rect,float,float,fz_matrix ctm,int,int)
{
    auto &s=state(d); s.tiles.push_back(s.skip);
    if(!s.skip) fallback(d,fz_transform_rect(area,ctm)); s.skip=true; return 0;
}
void endTile(fz_context *,fz_device *d)
{ auto &s=state(d); if(!s.tiles.empty()) { s.skip=s.tiles.back(); s.tiles.pop_back(); } }
void drop(fz_context *,fz_device *d)
{
    auto &s=state(d);
    for(size_t i=0;i<s.clips.size();++i) s.painter->restore();
    delete &s;
}
}
fz_device *newVectorPrintDevice(fz_context *ctx,QPainter &p,const QRect &page,QRegion &fallbackRegions)
{
    auto *d=fz_new_derived_device(ctx,Device);
    d->state=new State{&p,page,&fallbackRegions};
    d->base.drop_device=drop;
    d->base.fill_path=fillPath; d->base.stroke_path=strokePath;
    d->base.clip_path=clipPath; d->base.clip_stroke_path=clipStroke;
    d->base.fill_text=fillText; d->base.stroke_text=strokeText;
    d->base.clip_text=clipText; d->base.clip_stroke_text=clipStrokeText;
    d->base.pop_clip=popClip;
    d->base.fill_image=fillImage; d->base.fill_image_mask=imageMask; d->base.clip_image_mask=clipImage;
    d->base.fill_shade=shade;
    d->base.begin_mask=beginMask;
    d->base.begin_group=beginGroup; d->base.end_group=endGroup;
    d->base.begin_tile=beginTile; d->base.end_tile=endTile;
    return &d->base;
}
