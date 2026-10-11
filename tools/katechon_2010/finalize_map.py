"""Stage 2: merge slivers, number the provinces and write the map files of the mod."""
import json, re, os, numpy as np, random
from scipy import ndimage
from PIL import Image
Image.MAX_IMAGE_PIXELS=None
V2='/Users/stefanbidzamov/Games/Victoria2/'
OUT=V2+'mod/katechon2010/'
MIN_PROVINCE=300      # pixels; smaller regions join a neighbour of the same state
MIN_COUNTRY=10        # a state smaller than this cannot be shown on this map at all
lab=np.load('labels_raw.npy'); H,W=lab.shape
key=np.load('v2_key.npy')
raw=json.load(open('regions_raw.json')); regions=raw['regions']
owner=np.array(['']+[r['owner'] for r in regions[1:]])
parent=np.arange(len(regions))
def find(a):
    while parent[a]!=a:
        parent[a]=parent[parent[a]]; a=parent[a]
    return a
def pairs(l):
    out={}
    for a,b in ((l[:,:-1],l[:,1:]),(l[:-1,:],l[1:,:]),(l[:,-1:],l[:,:1])):
        m=(a!=b)&(a>0)&(b>0)
        x=a[m].astype(np.int64); y=b[m].astype(np.int64)
        lo=np.minimum(x,y); hi=np.maximum(x,y)
        k,c=np.unique(lo*100000+hi,return_counts=True)
        for kk,cc in zip(k,c):
            out[(int(kk//100000),int(kk%100000))]=out.get((int(kk//100000),int(kk%100000)),0)+int(cc)
    return out
for it in range(12):
    counts=np.bincount(lab.ravel(),minlength=len(regions))
    owner_px={}
    for i in np.nonzero(counts)[0]:
        if i: owner_px[owner[i]]=owner_px.get(owner[i],0)+int(counts[i])
    nb={}
    for (a,b),c in pairs(lab).items():
        nb.setdefault(a,{})[b]=c; nb.setdefault(b,{})[a]=c
    changed=0
    remap=np.arange(len(regions))
    used=set()
    for i in np.argsort(counts):
        if i==0 or counts[i]==0 or counts[i]>=MIN_PROVINCE or i in used: continue
        cands=nb.get(int(i),{})
        same={j:c for j,c in cands.items() if owner[j]==owner[i] and j not in used}
        target=None
        if same: target=max(same,key=same.get)
        elif owner_px[owner[i]]<MIN_COUNTRY and cands:
            other={j:c for j,c in cands.items() if j not in used}
            if other: target=max(other,key=other.get)
        if target is not None:
            remap[i]=target; used.add(int(i)); used.add(int(target)); changed+=1
            regions[target].setdefault('merged',[]).append(regions[i]['name'])
    lab=remap[lab]
    print('pass',it,'merged',changed,'provinces',len(np.unique(lab))-1)
    if not changed: break
# islands and enclaves too small to click join the nearest province of their state
counts=np.bincount(lab.ravel(),minlength=len(regions))
tiny=[i for i in np.nonzero(counts)[0] if i and counts[i]<8]
for i in tiny:
    same=[j for j in np.nonzero(counts)[0] if j and j!=i and owner[j]==owner[i] and counts[j]>=8]
    ys,xs=np.nonzero(lab==i)
    if same:
        cy,cx=ys.mean(),xs.mean()
        best=None
        for j in same:
            yy,xx=np.nonzero(lab==j) if counts[j]<4000 else (None,None)
            if yy is None: continue
            d=np.min(np.hypot(yy-cy,np.minimum(abs(xx-cx),W-abs(xx-cx))))
            if best is None or d<best[0]: best=(d,j)
        if best and best[0]<140:
            lab[lab==i]=best[1]
counts=np.bincount(lab.ravel(),minlength=len(regions))
live=[int(i) for i in np.nonzero(counts)[0] if i]
owners=sorted({owner[i] for i in live})
print('final provinces',len(live),'states',len(owners))
# province ids: sea provinces keep theirs, land takes the freed land ids and then new ones
defs={}; names={}
for line in open(V2+'map/definition.csv',encoding='latin-1'):
    p=line.rstrip('\r\n').split(';')
    if len(p)>=5 and p[0].isdigit():
        defs[int(p[0])]=(int(float(p[1])),int(float(p[2])),int(float(p[3]))); names[int(p[0])]=p[4]
dm=open(V2+'map/default.map',encoding='latin-1').read()
sea=sorted(set(int(x) for x in re.search(r'sea_starts\s*=\s*\{([^}]*)\}',dm,re.S).group(1).split()))
old_land=sorted(i for i in defs if i not in sea)
maxid=max(defs)
# Compact numbering: land first, then the sea provinces in their old order, so no id is left without pixels.
ids=list(range(1,len(live)+1))
sea_new={old:len(live)+1+k for k,old in enumerate(sea)}
used_colors={(r<<16)|(g<<8)|b for r,g,b in defs.values()}
rng=random.Random(2010)
# order provinces by state then west-to-east so ids are stable and readable
cent=ndimage.center_of_mass(np.ones_like(lab),lab,live)
live_sorted=sorted(range(len(live)),key=lambda k:(owner[live[k]],cent[k][1]))
provinces=[]
lut=np.zeros(len(regions),np.uint32); idlut=np.zeros(len(regions),np.int32)
for n,k in enumerate(live_sorted):
    i=live[k]
    while True:
        c=(rng.randrange(20,236)<<16)|(rng.randrange(20,236)<<8)|rng.randrange(20,236)
        if c not in used_colors: used_colors.add(c); break
    lut[i]=c; idlut[i]=ids[n]
    r=regions[i]
    provinces.append({'id':ids[n],'name':r['name'],'name_ru':r['name_ru'],'owner':r['owner'],'adm0':r['adm0'],'group':r['group'],'lat':r['lat'],'lon':r['lon'],
                      'area':int(counts[i]),'x':float(cent[k][1]),'y':float(cent[k][0]),'color':[c>>16,(c>>8)&255,c&255],'merged':r.get('merged',[])})
land=lab>0
newkey=np.where(land,lut[lab],key)
rgb=np.dstack([(newkey>>16)&255,(newkey>>8)&255,newkey&255]).astype(np.uint8)
os.makedirs(OUT+'map',exist_ok=True)
Image.fromarray(rgb,'RGB').save(OUT+'map/provinces.bmp')
np.save('province_ids.npy',np.where(land,idlut[lab],0).astype(np.int32))
# old province under each new province centre: used to inherit continent and climate
old_by_color={(r<<16)|(g<<8)|b:i for i,(r,g,b) in defs.items()}
ys,xs=np.nonzero(land)
for p in provinces:
    m=np.load('province_ids.npy')==p['id'] if False else None
pid=np.load('province_ids.npy')
flat_old=np.vectorize(lambda c:old_by_color.get(int(c),0),otypes=[np.int32])
# sample: most common old land province under each new province
order=np.argsort(pid.ravel(),kind='stable'); sp=pid.ravel()[order]; so=key.ravel()[order]
u,st,cn=np.unique(sp,return_index=True,return_counts=True)
under={}
for uu,s,c in zip(u,st,cn):
    if uu==0: continue
    vals,vc=np.unique(so[s:s+c],return_counts=True)
    best=[old_by_color.get(int(v),0) for v in vals[np.argsort(-vc)]]
    best=[b for b in best if b and b not in sea]
    under[int(uu)]=best[0] if best else 0
for p in provinces: p['old']=under.get(p['id'],0)
with open(OUT+'map/definition.csv','w',encoding='latin-1',errors='replace') as f:
    f.write('province;red;green;blue;x;x\n')
    rows={sea_new[i]:(defs[i],names[i]) for i in sea}
    for p in provinces: rows[p['id']]=(tuple(p['color']),p['name'])
    for i in sorted(rows):
        (r,g,b),nm=rows[i]; f.write('%d;%d;%d;%d;%s;x\n'%(i,r,g,b,nm.replace(';',' ')))
newmax=len(live)+len(sea)+1
dm2=re.sub(r'max_provinces\s*=\s*\d+','max_provinces = %d'%newmax,dm)
dm2=re.sub(r'sea_starts\s*=\s*\{[^}]*\}','sea_starts = {\n\t'+' '.join(str(sea_new[i]) for i in sea)+'\n}',dm2,flags=re.S)
open(OUT+'map/default.map','w',encoding='latin-1').write(dm2)
rs=open(V2+'map/region_sea.txt',encoding='latin-1').read()
rs=re.sub(r'\{([^}]*)\}',lambda m:'{ '+' '.join(str(sea_new.get(int(x),0)) for x in m.group(1).split() if x.isdigit() and int(x) in sea_new)+' }',rs)
open(OUT+'map/region_sea.txt','w',encoding='latin-1').write(rs)
json.dump({'provinces':provinces,'sea':[sea_new[i] for i in sea],'info':raw['info']},open('provinces.json','w'))
print('max_provinces',newmax,'smallest',sorted(p['area'] for p in provinces)[:8])
big=sorted(((sum(1 for p in provinces if p['owner']==o),o) for o in owners),reverse=True)[:12]; print(big)
