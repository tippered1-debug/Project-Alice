"""Draws the materials of the KATECHON shell into assets/katechon_shell_*.png.

One bar of state across the top, in blue lacquer over an aged-gold moulding, with
the clock let into it as a plaque and the eagle on a crimson shield hung from
its middle; a ribbon of windows down the left edge; the panel a dossier is set on. Everything is drawn at twice the size it is shown at; the
sizes are matched in src/gui/gui_map_hud.hpp (shell_*, plate_*).
"""
import math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
HERE=os.path.dirname(os.path.abspath(__file__)); ASSETS=os.path.join(HERE,'..','..','assets')
S=2            # pixels per point
SS=4           # supersampling of drawn shapes
rng=np.random.default_rng(1453)
GOLD_HI=np.array([246,216,150],np.float32); GOLD_LO=np.array([150,104,44],np.float32)

def blur(a,r): return np.asarray(Image.fromarray((np.clip(a,0,1)*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r)),np.float32)/255
def gild(mask,relief=1.4,hi=GOLD_HI,lo=GOLD_LO):
    """Metal leaf over a shape: lit from the upper left, darker towards its foot."""
    h,w=mask.shape
    b=blur(mask,relief)
    light=np.roll(np.roll(b,1,0),1,1)-np.roll(np.roll(b,-1,0),-1,1)
    ys=np.nonzero(mask.max(1)>0.02)[0]
    t=np.clip((np.arange(h)-ys[0])/max(1,ys[-1]-ys[0]),0,1)[:,None] if len(ys) else np.zeros((h,1))
    rgb=hi[None,None]*(1-0.62*t[...,None])+lo[None,None]*(0.62*t[...,None])
    rgb=rgb*(1+2.0*light[...,None])
    return np.dstack([np.clip(rgb,0,255),mask*255]).astype(np.uint8)
def canvas(w,h):
    im=Image.new('L',(int(w*S*SS),int(h*S*SS)),0); return im,ImageDraw.Draw(im)
def done(im): return np.asarray(im.resize((im.width//SS,im.height//SS),Image.LANCZOS),np.float32)/255
def P(pts): return [(x*S*SS,y*S*SS) for x,y in pts]
def over(base,top):
    b=Image.fromarray(base,'RGBA'); b.alpha_composite(Image.fromarray(top,'RGBA')); return np.asarray(b).copy()
def shadow(mask,r,dx,dy,strength):
    a=blur(np.roll(np.roll(mask,dy,0),dx,1),r)*strength
    return np.dstack([np.zeros(mask.shape+(3,)),a*255]).astype(np.uint8)
def save(a,name): Image.fromarray(a,'RGBA').save(os.path.join(ASSETS,name)); print(name,a.shape[1],a.shape[0])

def material(w,h,top,bottom,brush,tooth):
    """A surface: a fall of tone from top to bottom, long strokes of the brush, a fine tooth."""
    t=(np.arange(h)/max(1,h-1))[:,None,None]
    rgb=np.asarray(top,np.float32)[None,None]*(1-t)+np.asarray(bottom,np.float32)[None,None]*t
    rgb=np.broadcast_to(rgb,(h,w,3)).copy()
    strokes=np.repeat(rng.random((h,1)).astype(np.float32)-0.5,w,1)
    strokes=np.asarray(Image.fromarray(((strokes+0.5)*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7)),np.float32)/255-0.5
    fine=rng.random((h,w)).astype(np.float32)-0.5
    return rgb*(1+brush*strokes+tooth*fine)[...,None]

AGED_HI=np.array([226,192,124],np.float32); AGED_LO=np.array([128,90,42],np.float32)    # gold that has seen some years
def patina(a,amount=0.10):
    """Wear: slow, irregular darkening over what is already painted."""
    h,w=a.shape[:2]
    m=np.asarray(Image.fromarray((rng.random((max(2,h//24),max(2,w//24)))*255).astype(np.uint8)).resize((w,h),Image.BICUBIC).filter(ImageFilter.GaussianBlur(6)),np.float32)/255
    a[...,:3]=np.clip(a[...,:3]*(1-amount*m[...,None]),0,255).astype(np.uint8)
    return a
def leather(w,h,top,bottom):
    surf=material(w,h,top,bottom,0.0,0.07)
    pores=np.asarray(Image.fromarray((rng.random((h,w))*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.1)),np.float32)/255-0.5
    return surf*(1+0.16*pores)[...,None]

# ---- the bar of the state: the walnut of the ribbon over an aged-gold moulding ----
# It is lit from above: bright along its top edge, falling into shade towards
# the moulding and towards the corners of the screen.
BAR_H,SHADOW,BW=56,10,1024
MOULD=8
w,h=BW*S,BAR_H*S
surf=material(w,h,(46,60,78),(22,30,41),0.10,0.05)
yy=np.arange(h)[:,None]; xx=np.arange(w)[None,:]
surf*= (1+0.26*np.exp(-(yy/(3.0*S))**2))[...,None]                         # the lit top edge
surf*= (1-0.22*np.clip((yy-(BAR_H-MOULD-14)*S)/(14.0*S),0,1)**1.6)[...,None]  # the shade gathering above the moulding
surf*= (0.80+0.20*np.clip(np.minimum(xx,w-1-xx)/(150.0*S),0,1)**0.7)[...,None]
bar=np.dstack([np.clip(surf,0,255),np.full((h,w),255.0)]).astype(np.uint8)
im,d=canvas(BW,BAR_H); d.rectangle(P([(0,2.6),(BW,3.4)]),fill=255); line=done(im)
bar=over(bar,gild(line,0.8,AGED_HI,AGED_LO))
# the moulding, in two steps: a cove in shadow, a round of gold, a fillet, a thin second round
for r in range((BAR_H-MOULD)*S,h):
    k=(r-(BAR_H-MOULD)*S)/(MOULD*S-1)
    if k<0.16: c=np.array([9,12,17])*(1.0+1.6*k)
    elif k<0.62:
        u=(k-0.16)/0.46; shade=math.sin(math.pi*u)**0.6
        c=AGED_LO*(1-shade)+AGED_HI*shade*(1.12-0.42*u)
    elif k<0.74: c=np.array([10,13,18])
    elif k<0.93:
        u=(k-0.74)/0.19; shade=math.sin(math.pi*u)**0.8
        c=AGED_LO*0.8*(1-shade)+AGED_HI*0.86*shade
    else: c=np.array([6,8,11])
    bar[r,:,:3]=np.clip(c,0,255)
bar=patina(bar,0.12)
full=np.zeros(((BAR_H+SHADOW)*S,w,4),np.uint8); full[:h]=bar
for r in range(SHADOW*S): full[h+r,:,3]=int(255*0.58*(1-r/(SHADOW*S))**2)
save(full,'katechon_shell_bar.png')

# ---- what is set on the bar. Each is three cells: left end, middle, right end ----
PH,PAD,CELL,R=40,8,28,5
def slab(name,top,bottom,sunk,rim=1.0):
    w,h=3*CELL,PH+2*PAD
    im,d=canvas(w,h); d.rounded_rectangle(P([(PAD,PAD),(w-PAD,PAD+PH)]),radius=R*S*SS,fill=255); body=done(im)
    im,d=canvas(w,h); d.rounded_rectangle(P([(PAD+1.5,PAD+1.5),(w-PAD-1.5,PAD+PH-1.5)]),radius=(R-1.3)*S*SS,fill=255); face=done(im)
    surf=material(w*S,h*S,top,bottom,0.07,0.05)
    a=np.zeros((h*S,w*S,4),np.uint8)
    if not sunk: a=shadow(body,3.5,0,3,0.62)                               # a raised piece throws a shadow on the bar
    edge=gild(body,1.2,AGED_HI,AGED_LO); edge[...,3]=(edge[...,3]*rim).astype(np.uint8)
    a=over(a,edge)
    a=over(a,np.dstack([np.clip(surf,0,255),face*255]).astype(np.uint8))
    if sunk:
        lip=blur(np.clip(face-np.roll(face,5,0),0,1),1.6)                   # the shadow its upper lip throws inwards
        a=over(a,np.dstack([np.zeros((h*S,w*S,3)),np.clip(lip*1.5,0,1)*face*190]).astype(np.uint8))
        glint=np.clip(face-np.roll(face,-2,0),0,1)                           # and the light along its lower one
        a=over(a,np.dstack([np.full((h*S,w*S,3),255.0)*np.array([1,0.86,0.62]),glint*40]).astype(np.uint8))
    else:
        glint=np.clip(face-np.roll(face,2,0),0,1)                            # a raised piece is lit along its upper edge
        a=over(a,np.dstack([np.full((h*S,w*S,3),255.0)*np.array([1,0.90,0.70]),glint*70]).astype(np.uint8))
        foot=blur(np.clip(face-np.roll(face,-6,0),0,1),2.0)
        a=over(a,np.dstack([np.zeros((h*S,w*S,3)),foot*face*110]).astype(np.uint8))
    a[:,CELL*S:2*CELL*S]=a[:,CELL*S+CELL*S//2:CELL*S+CELL*S//2+1]
    save(a,name)
slab('katechon_shell_time.png',(36,48,64),(54,70,90),True)                  # the clock: let in, a shade lighter than the bar
slab('katechon_shell_well.png',(12,16,22),(26,34,45),True,0.55)             # a figure: let in, and darker
slab('katechon_shell_raised.png',(62,80,104),(34,45,60),False)              # the state: standing proud of the bar

# ---- the keystone: a small shield with the eagle, hung from the middle of the bar ----
CW,CH=66,70
def shield(inset):
    a=inset; top=2+inset; side=38
    pts=[(3+a,top),(CW-3-a,top),(CW-3-a,side)]
    for i in range(1,41):
        u=i/40; x0,y0=CW-3-a,side; x1,y1=CW-3-a,CH-12-a*0.4; x2,y2=CW/2,CH-2-a*1.3
        pts.append(((1-u)**2*x0+2*u*(1-u)*x1+u*u*x2,(1-u)**2*y0+2*u*(1-u)*y1+u*u*y2))
    for i in range(1,41):
        u=i/40; x0,y0=CW/2,CH-2-a*1.3; x1,y1=3+a,CH-12-a*0.4; x2,y2=3+a,side
        pts.append(((1-u)**2*x0+2*u*(1-u)*x1+u*u*x2,(1-u)**2*y0+2*u*(1-u)*y1+u*u*y2))
    return pts
im,d=canvas(CW,CH); d.polygon(P(shield(0)),fill=255); outer=done(im)
im,d=canvas(CW,CH); d.polygon(P(shield(2.4)),fill=255); inner=done(im)
h,w=outer.shape
field=leather(w,h,(156,30,36),(78,12,18))
a=shadow(outer,4,0,3,0.6)
a=over(a,np.dstack([np.clip(field,0,255),inner*255]).astype(np.uint8))
a=over(a,gild(np.clip(outer-inner,0,1),1.2,AGED_HI,AGED_LO))
crest=Image.open(os.path.join(ASSETS,'katechon_title_crest.png')).getchannel('A')
eh=int(50*S); ew=int(crest.width*eh/crest.height)
eagle=np.zeros((h,w),np.float32); ex=(w-ew)//2; ey=int(7*S)
eagle[ey:ey+eh,ex:ex+ew]=np.asarray(crest.resize((ew,eh),Image.LANCZOS),np.float32)/255
eagle=np.clip(eagle*1.8,0,1)
a=over(a,shadow(eagle,1.0,1,1,0.8)); a=over(a,gild(eagle,0.9))
save(a,'katechon_shell_keystone.png')

# ---- the frame of the flag ----
FW,FH=56,40
im,d=canvas(FW,FH); d.rectangle(P([(2,2),(FW-2,FH-2)]),fill=255); d.rectangle(P([(5,5),(FW-5,FH-5)]),fill=0); fr=done(im)
im,d=canvas(FW,FH); d.rectangle(P([(1,1),(FW-1,FH-1)]),fill=255); solid=done(im)
a=shadow(solid,2.5,0,2,0.6)
a[...,3]=(a[...,3]*(1-np.clip(blur(np.pad(np.ones(((FH-10)*S,(FW-10)*S),np.float32),((5*S,5*S),(5*S,5*S))),0.5),0,1))).astype(np.uint8)
a=over(a,gild(fr,1.3,AGED_HI,AGED_LO))
save(a,'katechon_shell_flagframe.png')

# ---- the ribbon of windows down the left edge, and its tail ----
RW=52
surf=material(RW*S,64*S,(28,37,50),(28,37,50),0.0,0.06)
surf*= (0.80+0.20*np.sin(np.pi*np.clip(np.arange(RW*S)/(RW*S-1),0,1))**0.5)[None,:,None]
rib=np.dstack([np.clip(surf,0,255),np.full((64*S,RW*S),255.0)]).astype(np.uint8)
im,d=canvas(RW,64); d.rectangle(P([(RW-2.4,0),(RW,64)]),fill=255); d.rectangle(P([(RW-5.6,0),(RW-5.0,64)]),fill=255); edge=done(im)
rib=over(rib,gild(edge,0.8,AGED_HI,AGED_LO))
rib[:]=rib[S:S+1]                                                  # every row the same: it is stretched to length
save(rib,'katechon_shell_ribbon.png')
TH=30
im,d=canvas(RW,TH); d.polygon(P([(0,0),(RW,0),(RW,TH-2),(RW/2,TH-14),(0,TH-2)]),fill=255); tail=done(im)
im,d=canvas(RW,TH); d.polygon(P([(0,0),(RW-2.4,0),(RW-2.4,TH-6.4),(RW/2,TH-17.2),(2.4,TH-6.4),(0,TH-2)]),fill=255); tail_in=done(im)
surf=material(RW*S,TH*S,(28,37,50),(19,25,34),0.0,0.06)
surf*= (0.80+0.20*np.sin(np.pi*np.clip(np.arange(RW*S)/(RW*S-1),0,1))**0.5)[None,:,None]
a=shadow(tail,3,0,3,0.55)
a=over(a,gild(tail,1.0,AGED_HI,AGED_LO))
a=over(a,np.dstack([np.clip(surf,0,255),tail_in*255]).astype(np.uint8))
save(a,'katechon_shell_ribbon_tail.png')

# ---- round pieces, one cell each: the seal behind an open window, the ring under the mouse,
# ---- and the buttons of the clock at rest, under the mouse, lit ----
B=48
def disc(r_out,r_in,face_in,face_out,hi=AGED_HI,lo=AGED_LO,face=True):
    c=B/2
    im,d=canvas(B,B); d.ellipse(P([(c-r_out,c-r_out),(c+r_out,c+r_out)]),fill=255); outer=done(im)
    im,d=canvas(B,B); d.ellipse(P([(c-r_in,c-r_in),(c+r_in,c+r_in)]),fill=255); inner=done(im)
    h,w=outer.shape; yy,xx=np.mgrid[0:h,0:w]; r=np.clip(np.hypot(xx-w/2,yy-h*0.42)/(w*0.40),0,1)[...,None]
    f=np.asarray(face_in,np.float32)[None,None]*(1-r)+np.asarray(face_out,np.float32)[None,None]*r
    f*= (1+0.10*(rng.random((h,w))-0.5))[...,None]
    a=shadow(outer,2.4,0,2,0.55) if face else np.zeros((h,w,4),np.uint8)
    if face: a=over(a,np.dstack([np.clip(f,0,255),inner*255]).astype(np.uint8))
    return over(a,gild(np.clip(outer-inner,0,1),1.1,hi,lo))
cells=[disc(21,18.6,(168,34,38),(86,13,18)),disc(21,19.6,(0,0,0),(0,0,0),face=False),
       disc(13,11.2,(50,64,84),(22,29,40)),disc(13,11.2,(86,104,130),(40,52,68)),disc(13,11.2,(196,48,44),(96,16,18))]
save(np.concatenate(cells,1),'katechon_shell_rounds.png')

# ---- the eagle of the state, small and gilt ----
crest=Image.open(os.path.join(ASSETS,'katechon_title_crest.png')).getchannel('A')
eh=44*S; ew=int(crest.width*eh/crest.height)
eagle=np.clip(np.asarray(crest.resize((ew,eh),Image.LANCZOS),np.float32)/255*2.4,0,1)
save(over(shadow(eagle,1.0,1,1,0.7),gild(eagle,0.9)),'katechon_shell_eagle.png')

# ---- the light behind the name of an open window ----
GW,GH=96,56
yy,xx=np.mgrid[0:GH*S,0:GW*S]
g=np.clip(np.exp(-(((xx-GW*S/2)/(GW*S*0.24))**2+((yy-GH*S*0.60)/(GH*S*0.22))**2))-0.04,0,1)
save(np.dstack([np.full(g.shape,255.0),np.full(g.shape,204.0),np.full(g.shape,128.0),g*96]).astype(np.uint8),'katechon_shell_glow.png')

# ---- the panel a dossier is set on ----
# A title in a deeper lacquer, over a calm, dark blue body. The
# game shows the top of the image down to the height it needs and closes it
# with the bottom PLATE_FOOT points, so below the head every row must look like
# the next.
PW,PLH,PLATE_FOOT,HEAD=344,640,160,104
w,h=PW*S,PLH*S
rgb=material(w,h,(27,36,48),(21,28,38),0.0,0.05)
yy,xx=np.mgrid[0:h,0:w]
rgb*= (0.88+0.12*np.clip((w-1-xx)/(70.0*S),0,1)**0.6)[...,None]
rgb*= (1-0.12*np.clip((yy-(PLH-PLATE_FOOT)*S)/(PLATE_FOOT*S),0,1))[...,None]
rgb[:HEAD*S]=leather(w,HEAD*S,(40,53,70),(26,35,47))
a=np.dstack([np.clip(rgb,0,255),np.full((h,w),255.0)]).astype(np.uint8)
eh=int(112*S); ew=int(crest.width*eh/crest.height)
mark=np.zeros((h,w),np.float32); ex=(w-ew)//2; ey=h-int(140*S)
mark[ey:ey+eh,ex:ex+ew]=np.asarray(crest.resize((ew,eh),Image.LANCZOS),np.float32)/255
m=gild(np.clip(mark*1.2,0,1),1.0); m[...,3]=(m[...,3]*0.06).astype(np.uint8)
a=over(a,m)
im,d=canvas(PW,PLH)
d.rectangle(P([(0,HEAD-1.6),(PW,HEAD+1.6)]),fill=255)               # the moulding under the title
d.rectangle(P([(0,HEAD-5.0),(PW,HEAD-4.4)]),fill=255)
d.rectangle(P([(PW-2.0,0),(PW,PLH)]),fill=255)                       # the edge towards the map
d.rectangle(P([(0,PLH-2.0),(PW,PLH)]),fill=255)                      # the foot
gilt=done(im)
a=over(a,shadow(gilt,1.5,0,1,0.7))
a=over(a,gild(gilt,1.0,AGED_HI,AGED_LO))
a=patina(a,0.12)
a[(HEAD+2)*S:(HEAD+9)*S,:,:3]=(a[(HEAD+2)*S:(HEAD+9)*S,:,:3]*(0.60+0.40*(np.arange(7*S)/(7*S))[:,None,None])).astype(np.uint8)   # the title casts a short shadow
save(a,'katechon_plate.png')

# ---- the PLAY button of the country selection: at rest, and under the mouse ----
# Crimson leather in a gilt frame with turned-in corners, a sprig of laurel at either end.
BW2,BH2=324,72
def play_button(lift):
    im,d=canvas(BW2,BH2); d.rounded_rectangle(P([(3,3),(BW2-3,BH2-3)]),radius=9*S*SS,fill=255); body=done(im)
    im,d=canvas(BW2,BH2); d.rounded_rectangle(P([(5.4,5.4),(BW2-5.4,BH2-5.4)]),radius=7*S*SS,fill=255); face=done(im)
    im,d=canvas(BW2,BH2); d.rounded_rectangle(P([(9.5,9.5),(BW2-9.5,BH2-9.5)]),radius=4*S*SS,outline=255,width=int(0.8*S*SS)); line=done(im)
    im,d=canvas(BW2,BH2)
    for side in (1,-1):
        base=BW2/2+side*(BW2/2-26)
        stem=[]
        for i in range(25):
            u=i/24; x=base-side*4*math.sin(u*math.pi); y=BH2/2+22-44*u
            stem.append((x,y))
        d.line(P(stem),fill=255,width=int(1.0*S*SS))
        for i in range(3,24,4):
            x,y=stem[i]
            for lean in (1,-1):
                ang=-math.pi/2+lean*0.62                      # leaves point up and outwards
                cx,cy=x+7.0*math.cos(ang),y+7.0*math.sin(ang)
                pts=[]
                for k in range(24):
                    t=2*math.pi*k/24; ex,ey=7.4*math.cos(t),2.7*math.sin(t)
                    pts.append((cx+ex*math.cos(ang)-ey*math.sin(ang),cy+ex*math.sin(ang)+ey*math.cos(ang)))
                d.polygon(P(pts),fill=255)
    laurel=done(im)
    h,w=body.shape
    top=np.array([150,30,36],np.float32)*lift; bot=np.array([86,14,20],np.float32)*lift
    surf=leather(w,h,tuple(np.clip(top,0,255)),tuple(np.clip(bot,0,255)))
    yy=np.arange(h)[:,None]; surf*= (1+0.18*np.exp(-((yy-6*S)/(4.0*S))**2))[...,None]
    a=shadow(body,4,0,4,0.7)
    a=over(a,gild(body,1.2,AGED_HI,AGED_LO))
    a=over(a,np.dstack([np.clip(surf,0,255),face*255]).astype(np.uint8))
    a=over(a,gild(line,0.8,AGED_HI,AGED_LO))
    return over(a,gild(laurel,0.8,AGED_HI,AGED_LO))
save(np.concatenate([play_button(1.0),play_button(1.28)],1),'katechon_pick_play.png')
