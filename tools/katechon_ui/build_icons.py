"""Draws the icon strip of the KATECHON map HUD: assets/katechon_hud_icons.png.

One row of square cells, white line drawings on transparent; the game tints
them. Each icon is described on a 24-unit grid. The order of ICONS is the order
of the enum `icon` in src/gui/gui_map_hud.hpp.
"""
import math, os
from PIL import Image, ImageDraw
CELL=36          # pixels per cell in the file: an 18-point icon on a Retina screen
SS=8             # supersampling
STROKE=1.7       # line width, grid units
K=CELL*SS/24.0

def arc(cx,cy,rx,ry,a0,a1,n=40):
    return [(cx+rx*math.cos(math.radians(a0+(a1-a0)*i/n)),cy+ry*math.sin(math.radians(a0+(a1-a0)*i/n))) for i in range(n+1)]
def star(cx,cy,R,r):
    return [(cx+(R if i%2==0 else r)*math.sin(math.pi*i/5),cy-(R if i%2==0 else r)*math.cos(math.pi*i/5)) for i in range(10)]
def gear(cx,cy,R,r,teeth=8):
    pts=[]
    for i in range(teeth):
        a=2*math.pi*i/teeth; w=2*math.pi/teeth
        for da,rad in ((-0.30,r),(-0.18,R),(0.18,R),(0.30,r)):
            pts.append((cx+rad*math.cos(a+da*w),cy+rad*math.sin(a+da*w)))
    return pts
def mirror(half,cx=12):
    # half: the right half of a symmetric outline, top centre to bottom centre, x as an offset from cx
    return [(cx+x,y) for x,y in half]+[(cx-x,y) for x,y in reversed(half)]
def scaled(half,s,dy):
    return [(x*s,12+(y-12)*s+dy) for x,y in half]
# the right half of a heraldic double-headed eagle, and of the shield on its breast
EAGLE=[(0,8.8),(0.9,7.6),(1.2,6.2),(2.0,4.8),(3.4,4.6),(4.8,5.5),(3.5,6.1),(2.7,6.8),(2.5,7.9),
       (3.3,8.0),(5.4,4.4),(7.8,3.0),(7.5,5.2),(8.6,5.4),(7.4,7.3),(8.6,7.7),(6.9,9.4),(7.8,10.0),(5.6,11.6),(5.6,12.4),
       (3.0,12.6),(4.0,14.0),(5.4,14.6),(4.6,15.4),(3.0,15.3),(1.5,15.0),(3.2,18.6),(1.6,18.4),(0,19.6)]
SHIELD=[(0,9.6),(1.5,9.9),(1.5,12.4),(0,13.8)]
# ('line', points, closed) | ('fill', points) | ('cut', points) | ('dot', x, y, r)
ICONS=[
 ('layers',[('line',[(12,3),(21,8),(12,13),(3,8)],True),('line',[(3,12.5),(12,17.5),(21,12.5)],False),('line',[(3,17),(12,22),(21,17)],False)]),
 ('capital',[('line',star(12,12.6,9.6,4.0),True)]),
 ('search',[('line',arc(10.5,10.5,6.6,6.6,0,360),True),('line',[(15.4,15.4),(21,21)],False)]),
 ('ledger',[('line',[(12,20),(10.4,18.4),(8.6,18),(3,18),(3,5),(8.6,5),(10.6,5.6),(12,7.2),(13.4,5.6),(15.4,5),(21,5),(21,18),(15.4,18),(13.6,18.4)],True),('line',[(12,7.2),(12,20)],False)]),
 ('economy',[('line',[(3,3),(3,21),(21,21)],False),('line',[(7,15.5),(11,10.5),(14.2,13.5),(20,6.5)],False)]),
 ('outliner',[('line',[(9,6),(21,6)],False),('line',[(9,12),(21,12)],False),('line',[(9,18),(21,18)],False),('dot',4.4,6,1.3),('dot',4.4,12,1.3),('dot',4.4,18,1.3)]),
 ('log',[('line',[(6,3),(14,3),(19,8),(19,21),(6,21)],True),('line',[(14,3),(14,8),(19,8)],False),('line',[(9,13),(16,13)],False),('line',[(9,17),(16,17)],False)]),
 ('menu',[('line',gear(12,12,9.8,7.4),True),('line',arc(12,12,3.1,3.1,0,360),True)]),
 ('minus',[('line',[(5,12),(19,12)],False)]),
 ('plus',[('line',[(5,12),(19,12)],False),('line',[(12,5),(12,19)],False)]),
 ('chevron_right',[('line',[(9,6),(15,12),(9,18)],False)]),
 ('person',[('line',arc(12,8,4,4,0,360),True),('line',arc(12,21,8,6.6,180,360),False)]),
 ('work',[('line',[(4,8),(20,8),(21,9),(21,19),(20,20),(4,20),(3,19),(3,9)],True),('line',[(9,8),(9,5.4),(9.6,4.8),(14.4,4.8),(15,5.4),(15,8)],False),('line',[(3,13.6),(21,13.6)],False)]),
 ('movement',[('line',[(4,8),(20,8)],False),('line',[(16,4),(20,8),(16,12)],False),('line',[(20,16),(4,16)],False),('line',[(8,12),(4,16),(8,20)],False)]),
 ('society',[('line',arc(9,8,3.3,3.3,0,360),True),('line',arc(9,20.5,6.6,5.8,180,360),False),('line',arc(16.4,8.4,2.9,2.9,-110,110),False),('line',arc(17.2,20.5,4.6,5.4,-80,0),False)]),
 ('growth',[('line',[(3,17),(9,11),(13,15),(21,7)],False),('line',[(15,7),(21,7),(21,13)],False)]),
 ('chevron_down',[('line',[(6,9),(12,15),(18,9)],False)]),
 ('pause',[('fill',[(7,5),(10.4,5),(10.4,19),(7,19)]),('fill',[(13.6,5),(17,5),(17,19),(13.6,19)])]),
 ('play',[('fill',[(8,4.6),(19.4,12),(8,19.4)])]),
 ('close',[('line',[(6,6),(18,18)],False),('line',[(18,6),(6,18)],False)]),
 ('home',[('line',[(3,11.6),(12,3.6),(21,11.6)],False),('line',[(5.6,9.6),(5.6,20.4),(18.4,20.4),(18.4,9.6)],False),('line',[(10,20.4),(10,14.4),(14,14.4),(14,20.4)],False)]),
 ('chevron_left',[('line',[(15,6),(9,12),(15,18)],False)]),
 ('chevron_up',[('line',[(6,15),(12,9),(18,15)],False)]),
 ('production',[('line',[(3,21),(3,12),(9,15.5),(9,12),(15,15.5),(15,4),(19,4),(21,21)],True),('line',[(6.5,18),(11,18)],False)]),
 ('budget',[('line',arc(12,12,9.6,9.6,0,360),True),('fill',mirror(scaled(EAGLE,0.8,0.2))),('cut',mirror(scaled(SHIELD,0.8,0.2)))]),
 ('technology',[('line',[(8.5,3),(15.5,3)],False),('line',[(10,3),(10,9),(4.6,18.6),(6,21),(18,21),(19.4,18.6),(14,9),(14,3)],False),('line',[(7.2,15),(16.8,15)],False)]),
 ('politics',[('line',[(3,9),(12,3),(21,9)],True),('line',[(6,11.5),(6,18)],False),('line',[(10,11.5),(10,18)],False),('line',[(14,11.5),(14,18)],False),('line',[(18,11.5),(18,18)],False),('line',[(3,21),(21,21)],False)]),
 ('trade',[('line',[(12,3),(12,20)],False),('line',[(7,21),(17,21)],False),('line',[(4,7),(20,7)],False),('line',[(5.5,7),(2.2,13.4)],False),('line',[(5.5,7),(8.8,13.4)],False),('line',arc(5.5,13.4,3.3,3.6,0,180),False),('line',[(18.5,7),(15.2,13.4)],False),('line',[(18.5,7),(21.8,13.4)],False),('line',arc(18.5,13.4,3.3,3.6,0,180),False)]),
 ('diplomacy',[('line',arc(12,12,9,9,0,360),True),('line',arc(12,12,4,9,0,360),True),('line',[(3,12),(21,12)],False),('line',arc(12,5.5,7,2.2,20,160),False),('line',arc(12,18.5,7,2.2,200,340),False)]),
 ('military',[('line',[(4,4),(18,18)],False),('line',[(20,4),(6,18)],False),('line',[(13.6,19.4),(19.4,13.6)],False),('line',[(4.6,13.6),(10.4,19.4)],False),('dot',20,20,1.4),('dot',4,20,1.4)]),
]
def stroke(d,pts,closed):
    p=[(x*K,y*K) for x,y in pts]
    if closed: p=p+[p[0]]
    w=STROKE*K; r=w/2
    d.line(p,fill=255,width=int(round(w)),joint='curve')
    for x,y in p: d.ellipse((x-r,y-r,x+r,y+r),fill=255)
strip=Image.new('L',(CELL*len(ICONS),CELL),0)
for i,(name,parts) in enumerate(ICONS):
    big=Image.new('L',(CELL*SS,CELL*SS),0); d=ImageDraw.Draw(big)
    for part in parts:
        if part[0]=='line': stroke(d,part[1],part[2])
        elif part[0]=='fill': d.polygon([(x*K,y*K) for x,y in part[1]],fill=255)
        elif part[0]=='cut': d.polygon([(x*K,y*K) for x,y in part[1]],fill=0)
        else: d.ellipse(((part[1]-part[3])*K,(part[2]-part[3])*K,(part[1]+part[3])*K,(part[2]+part[3])*K),fill=255)
    strip.paste(big.resize((CELL,CELL),Image.LANCZOS),(i*CELL,0))
white=Image.new('L',strip.size,255)
out=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..','assets','katechon_hud_icons.png')
Image.merge('RGBA',(white,white,white,strip)).save(out)
print(len(ICONS),'icons ->',os.path.normpath(out))
