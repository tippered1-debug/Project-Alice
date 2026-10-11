"""Stage 1: rasterise 2010 first-level regions onto the game's land mask.

Outputs labels.npy (per-pixel region index, 0 = water) and regions.json.
"""
import json, numpy as np, cv2, geo
from scipy import ndimage
W,H=geo.W,geo.H
land=np.load('v2_land.npy')
a0=json.load(open('ne_10m_admin_0_countries.geojson'))['features']
a1=json.load(open('ne_10m_admin_1_states_provinces.geojson'))['features']

# sovereign code -> the ISO-like code of the sovereign state itself
sov_main={}
for f in a0:
    p=f['properties']
    if p['ADMIN']==p['SOVEREIGNT']:
        sov_main[p['SOV_A3']]=p['ADM0_A3']
for f in a0:
    p=f['properties']
    if p['TYPE'] in ('Sovereign country','Country') and p['SOV_A3'] not in sov_main:
        sov_main[p['SOV_A3']]=p['ADM0_A3']
for f in a0:
    p=f['properties']
    if p['SOV_A3'] not in sov_main and p['SOV_A3']==p['ADM0_A3']:
        sov_main[p['SOV_A3']]=p['ADM0_A3']
# The political map of 1 January 2010.
OVERRIDE={'SDS':'SDN',   # South Sudan became independent in July 2011
          'SOL':'SOM','CYN':'CYP','CNM':'CYP','ESB':'CYP','WSB':'CYP','KAS':'IND','KAB':'KAZ','USG':'CUB',
          'SAH':'MAR','AND':'ADR','BRI':'BRA','BRT':'SDN','PSX':'PSX','KOS':'KOS','TWN':'TWN','BJN':'COL','SER':'COL','SCR':'PHL','PGA':'PHL','SPI':'PHL'}
def owner_of(adm0,sov):
    if adm0 in OVERRIDE: return OVERRIDE[adm0]
    o=sov_main.get(sov,adm0)
    return {'AND':'ADR'}.get(o,o)   # AND is a keyword in the game's script language
info={p['ADM0_A3']:p for p in (f['properties'] for f in a0)}

def rings_of(geom):
    polys=[geom['coordinates']] if geom['type']=='Polygon' else geom['coordinates']
    for poly in polys:
        yield poly
def draw(img,geom,value):
    for poly in rings_of(geom):
        pts=[]
        for k,ring in enumerate(poly):
            r=np.asarray(ring,dtype=float)
            ref=np.median(r[:,0])
            lon=((r[:,0]-ref+180)%360)-180+ref          # continuous within the ring
            if lon.mean()<-170: lon=lon+360
            x,y=geo.to_pixel(np.clip(lon,-170,190),r[:,1])
            x=x+(lon-np.clip(lon,-170,190))/360*W
            pts.append(np.stack([x+W,y],1).round().astype(np.int32))
        cv2.fillPoly(img,pts,int(value))

regions=[None]
img=np.zeros((H,W*3),np.int32)
order=sorted(range(len(a1)),key=lambda i:-(a1[i]['properties'].get('area_sqkm') or 0))
for i in order:
    f=a1[i]; p=f['properties']
    if p['adm0_a3']=='ATA' or f['geometry'] is None: continue
    owner=owner_of(p['adm0_a3'],p['sov_a3'])
    name=p.get('name_en') or p.get('name') or p.get('gn_name') or p['adm1_code']
    # Crimea and Sevastopol were Ukrainian in 2010.
    if p['adm0_a3']=='RUS' and (p.get('iso_3166_2') in ('UA-43','UA-40') or name in ('Crimea','Sevastopol','Republic of Crimea')): owner='UKR'
    regions.append({'name':name,'name_ru':p.get('name_ru') or name,'owner':owner,'adm0':p['adm0_a3'],'code':p['adm1_code'],
                    'lat':p.get('latitude'),'lon':p.get('longitude'),'group':p.get('region') or ''})
    draw(img,f['geometry'],len(regions)-1)
lab=np.maximum(np.maximum(img[:,:W],img[:,W:2*W]),img[:,2*W:])
# countries with no first-level regions in the dataset
have={r['adm0'] for r in regions[1:]}
for f in a0:
    p=f['properties']
    if p['ADM0_A3'] in have or p['ADM0_A3']=='ATA': continue
    regions.append({'name':p['NAME_LONG'],'name_ru':p.get('NAME_RU') or p['NAME_LONG'],'owner':owner_of(p['ADM0_A3'],p['SOV_A3']),'adm0':p['ADM0_A3'],'code':p['ADM0_A3'],'lat':p.get('LABEL_Y'),'lon':p.get('LABEL_X'),'group':''})
    tmp=np.zeros((H,W*3),np.int32); draw(tmp,f['geometry'],len(regions)-1)
    t=np.maximum(np.maximum(tmp[:,:W],tmp[:,W:2*W]),tmp[:,2*W:])
    lab=np.where((lab==0)&(t>0),t,lab)
print('regions',len(regions)-1,'labelled land share',((lab>0)&land).sum()/land.sum())
# game land with no region underneath takes the nearest region
idx=ndimage.distance_transform_edt(lab==0,return_distances=False,return_indices=True)
full=lab[idx[0],idx[1]]
lab=np.where(land,full,0).astype(np.int32)
np.save('labels_raw.npy',lab)
json.dump({'regions':regions,'info':{k:{kk:v[kk] for kk in ('NAME','NAME_LONG','FORMAL_EN','POP_EST','GDP_MD','ECONOMY','INCOME_GRP','CONTINENT','SUBREGION','REGION_WB','NAME_RU','ISO_A2','MAPCOLOR13') if kk in v} for k,v in info.items()}},open('regions_raw.json','w'))
owners=sorted({r['owner'] for r in regions[1:]})
print('owners',len(owners)); print(' '.join(owners))
