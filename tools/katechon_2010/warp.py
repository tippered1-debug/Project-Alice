import json, re, numpy as np, unicodedata
from scipy.interpolate import RBFInterpolator
from scipy import ndimage
V2='/Users/stefanbidzamov/Games/Victoria2/'
key=np.load('v2_key.npy'); H,W=key.shape
names={}; colors={}
for line in open(V2+'map/definition.csv',encoding='latin-1'):
    p=line.strip().split(';')
    if len(p)>=5 and p[0].isdigit():
        colors[int(p[0])]=(int(float(p[1]))<<16)|(int(float(p[2]))<<8)|int(float(p[3])); names[int(p[0])]=p[4]
def norm(s):
    s=unicodedata.normalize('NFKD',s).encode('ascii','ignore').decode().lower()
    return re.sub(r'[^a-z]','',s)
# centroids of provinces
flat=key.ravel()
order=np.argsort(flat,kind='stable'); sk=flat[order]
ys,xs=np.divmod(order,W)
cent={}
uniq,start,count=np.unique(sk,return_index=True,return_counts=True)
col2id={c:i for i,c in colors.items()}
for c,s,n in zip(uniq,start,count):
    i=col2id.get(int(c))
    if i is None: continue
    x=xs[s:s+n]; y=ys[s:s+n]
    # handle wrap: use median
    cent[i]=(float(np.median(x)),float(np.median(y)),int(n))
pp=json.load(open('ne_10m_populated_places_simple.geojson'))['features']
places={}
for f in pp:
    pr=f['properties']
    for nm in {pr.get('name'),pr.get('nameascii'),pr.get('namealt')}:
        if nm:
            for part in str(nm).split('|'):
                places.setdefault(norm(part),[]).append((pr['longitude'],pr['latitude'],pr.get('pop_max',0)))
def miller(lat): return 1.25*np.log(np.tan(np.pi/4+0.4*np.radians(lat)))
def rough(lon,lat):
    x=((lon+170.0)%360)/360*W
    y=(miller(88)-miller(lat))/(miller(88)-miller(-52))*H
    return x,y
src=[];dst=[];who=[]
for i,(cx,cy,n) in cent.items():
    cands=places.get(norm(names[i]),[])
    if not cands or len(norm(names[i]))<4: continue
    best=None
    for lon,lat,pop in cands:
        rx,ry=rough(lon,lat); dx=min(abs(rx-cx),W-abs(rx-cx)); d=np.hypot(dx,ry-cy)
        if best is None or d<best[0]: best=(d,lon,lat)
    if best[0]<450:
        src.append((best[1],best[2])); dst.append((cx,cy)); who.append(i)
src=np.array(src); dst=np.array(dst); print('initial matches',len(src))
# work in unwrapped lon relative to -170 so the seam is at the map edge
def feat(ll):
    lon=(ll[:,0]+170.0)%360; return np.stack([lon/360*W, (miller(88)-miller(ll[:,1]))/(miller(88)-miller(-52))*H],1)
keep=np.ones(len(src),bool)
for it in range(5):
    F=feat(src[keep]); D=dst[keep]-F
    rbf=RBFInterpolator(F,D,kernel='thin_plate_spline',smoothing=len(F)*40.0,neighbors=60)
    res=np.linalg.norm(rbf(feat(src))+feat(src)-dst,axis=1)
    thr=max(25,np.percentile(res[keep],90))
    keep=res<thr
    print(it,'kept',keep.sum(),'median res',np.median(res[keep]),'p90',np.percentile(res[keep],90))
np.savez('warp_ctrl.npz',src=src[keep],dst=dst[keep])
