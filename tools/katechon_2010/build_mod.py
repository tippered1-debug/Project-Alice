"""Stage 3: write the KATECHON 2010 scenario as a mod over the base game data."""
import json, os, re, glob, colorsys, hashlib, unicodedata, shutil
import numpy as np
import geo
from countries_2010 import COUNTRY, GOVERNMENT
V2='/Users/stefanbidzamov/Games/Victoria2/'
MOD='katechon2010'
OUT=V2+'mod/'+MOD+'/'
D=json.load(open('provinces.json')); P=D['provinces']; INFO=D['info']
pid=np.load('province_ids.npy'); H,W=pid.shape
byid={p['id']:p for p in P}
owners=sorted({p['owner'] for p in P})
def w(path,text,enc='latin-1'):
    os.makedirs(os.path.dirname(OUT+path),exist_ok=True)
    open(OUT+path,'w',encoding=enc,errors='replace').write(text)
def ascii_(s):
    return unicodedata.normalize('NFKD',s).encode('ascii','ignore').decode().replace(';',' ').replace('"','').strip() or 'Unnamed'
def rd(path): return re.sub(r'#.*','',open(V2+path,encoding='latin-1').read())

# ---------- country facts ----------
ISO_FIX={'KOS':'XKX','PSX':'PSE','ADR':'AND'}
def wb(ind):
    out={}
    for r in json.load(open('wb_%s.json'%ind))[1]:
        if r['value'] is not None: out[r['countryiso3code']]=float(r['value'])
    return out
POP=wb('SP.POP.TOTL'); GDP=wb('NY.GDP.MKTP.CD'); RUR=wb('SP.RUR.TOTL.ZS')
POP['SDN']=POP.get('SDN',34e6)+POP.get('SSD',9.5e6); POP.setdefault('TWN',23.16e6); GDP.setdefault('TWN',446e9)
def fact(tag,table,default):
    return table.get(ISO_FIX.get(tag,tag),default)
def info(tag): return INFO.get({'ADR':'AND'}.get(tag,tag),{})
def population(tag):
    return fact(tag,POP,None) or max(1000.0,0.9*float(info(tag).get('POP_EST') or 100000))
def gdp(tag):
    v=fact(tag,GDP,None)
    return v if v else population(tag)*900.0
def income(tag):
    g=str(info(tag).get('INCOME_GRP','4'))[:1]
    return {'1':0,'2':0,'3':1,'4':2,'5':3}.get(g,2)
NAME_FIX={'PSX':'Palestine','KOS':'Kosovo','COD':'DR Congo','COG':'Congo','PRK':'North Korea','KOR':'South Korea','LAO':'Laos','RUS':'Russia','USA':'United States','GBR':'United Kingdom','CIV':'Ivory Coast','TLS':'East Timor','CZE':'Czech Republic','MKD':'Macedonia','SWZ':'Swaziland','FSM':'Micronesia','BRN':'Brunei','SYR':'Syria','IRN':'Iran','VNM':'Vietnam','TZA':'Tanzania','MDA':'Moldova','BOL':'Bolivia','VEN':'Venezuela'}
def cname(tag): return NAME_FIX.get(tag) or ascii_(info(tag).get('NAME_LONG') or info(tag).get('NAME') or tag)

# ---------- map side files ----------
old_cont={}; cont_blocks=[]
ct=rd('map/continent.txt')
for m in re.finditer(r'(\w+)\s*=\s*\{\s*provinces\s*=\s*\{([^}]*)\}([^}]*)\}',ct,re.S):
    cont_blocks.append((m.group(1),m.group(3)))
    for x in m.group(2).split(): old_cont[int(x)]=m.group(1)
CONT_OF={'Europe':'europe','Asia':'asia','Africa':'africa','North America':'north_america','South America':'south_america','Oceania':'oceania'}
cont_names=[c for c,_ in cont_blocks]
def continent(p):
    c=old_cont.get(p['old'])
    if c: return c
    g=CONT_OF.get(info(p['adm0']).get('CONTINENT',''),'asia')
    return g if g in cont_names else cont_names[0]
by_cont={}
for p in P: by_cont.setdefault(continent(p),[]).append(p['id'])
w('map/continent.txt','\n'.join('%s = {\n\tprovinces = {\n\t\t%s\n\t}\n%s}\n'%(c,' '.join(map(str,sorted(by_cont.get(c,[])))),rest) for c,rest in cont_blocks))
cl=rd('map/climate.txt')
old_clim={}; clim_defs=[]
for m in re.finditer(r'(\w+_climate)\s*=\s*\{([^}]*)\}',cl,re.S):
    body=m.group(2)
    if re.search(r'[a-z_]+\s*=',body): clim_defs.append((m.group(1),body))
    else:
        for x in body.split(): old_clim[int(x)]=m.group(1)
clim_names=[c for c,_ in clim_defs]
def climate(p):
    c=old_clim.get(p['old'])
    if c: return c
    lat=abs(p['lat'] or 30)
    return clim_names[min(len(clim_names)-1,0 if lat<35 else 1 if lat<50 else 2 if lat<62 else 3)]
by_clim={}
for p in P: by_clim.setdefault(climate(p),[]).append(p['id'])
w('map/climate.txt','\n'.join('%s = {%s}\n'%(c,b) for c,b in clim_defs)+'\n'+'\n'.join('%s = {\n\t%s\n}\n'%(c,' '.join(map(str,sorted(v)))) for c,v in by_clim.items()))
w('map/adjacencies.csv','#From;To;Type;Through;Data;Comment\n')
w('map/positions.txt','')
# states: provinces of one country sharing a named region, otherwise west-to-east runs of four
states=[]
for o in owners:
    mine=[p for p in P if p['owner']==o]
    groups={}
    for p in mine: groups.setdefault(p['group'] or '',[]).append(p)
    for g,ps in sorted(groups.items()):
        ps.sort(key=lambda p:(p['x'],p['y']))
        if g: chunks=[ps[i:i+6] for i in range(0,len(ps),6)]
        else: chunks=[ps[i:i+4] for i in range(0,len(ps),4)]
        for ch in chunks:
            big=max(ch,key=lambda p:p['area'])
            # The first province listed is the state capital; the game keeps officials,
            # capitalists and landowners there, so the population is authored that way.
            states.append((o,ascii_(g) if g and len(chunks)==1 else ascii_(big['name']),[big['id']]+[p['id'] for p in ch if p is not big]))
            for p in ch: p['state_capital']=big['id']
state_keys=[]
txt=''
for n,(o,name,ids) in enumerate(states,1):
    key='KAT_%d'%n; state_keys.append((key,name)); txt+='%s = { %s }\n'%(key,' '.join(map(str,ids)))
    for i in ids: byid[i]['state']=key
w('map/region.txt',txt)

# ---------- population ----------
places=json.load(open('ne_10m_populated_places_simple.geojson'))['features']
urban={p['id']:0.0 for p in P}; capital={}
for f in places:
    pr=f['properties']
    x,y=geo.to_pixel(np.array([pr['longitude']]),np.array([pr['latitude']]))
    xi=int(round(float(x[0])))%W; yi=int(round(float(y[0])))
    if not (0<=yi<H): continue
    i=int(pid[yi,xi])
    if i==0:
        ys,xs=np.nonzero(pid[max(0,yi-6):yi+7,max(0,xi-6):min(W,xi+7)])
        if len(ys)==0: continue
        k=np.argmin((ys-6)**2+(xs-6)**2); i=int(pid[max(0,yi-6)+ys[k],max(0,xi-6)+xs[k]])
    urban[i]+=float(pr.get('pop_max') or 0)
    if pr['featurecla'].startswith('Admin-0 capital') and byid[i]['owner']==pr['adm0_a3'] and pr['adm0_a3'] not in capital:
        capital[pr['adm0_a3']]=i
SHARES={ # farmers labourers craftsmen clerks artisans bureaucrats clergymen soldiers officers capitalists aristocrats
 0:[3,4,22,48,10,7,1.5,1,0.3,3,0.2], 1:[14,8,28,28,10,6,1.5,1.2,0.3,2.5,0.5], 2:[35,12,22,12,10,4,1.5,1,0.2,1.5,0.8], 3:[58,12,10,4,9,3,1.5,1,0.2,0.6,0.7]}
TYPES=['farmers','labourers','craftsmen','clerks','artisans','bureaucrats','clergymen','soldiers','officers','capitalists','aristocrats']
ROLE={'farmers':'peasant','labourers':'peasant','aristocrats':'landed'}
CAPITAL_ONLY=('aristocrats','bureaucrats','capitalists')
# One simulated person stands for PEOPLE_PER_PERSON real people; the game counts one pop unit per four persons.
PEOPLE_PER_PERSON=float(os.environ.get('KATECHON_PEOPLE_PER_PERSON','5000'))
SCALE=4.0*PEOPLE_PER_PERSON
prov_pop={}
ALL_SIZES={}
pops_txt={}
households=[]
for o in owners:
    mine=[p for p in P if p['owner']==o]
    total=population(o); r=fact(o,RUR,45.0)/100.0
    su=sum(urban[p['id']] for p in mine); sa=sum(p['area'] for p in mine)
    if o not in capital: capital[o]=max(mine,key=lambda p:(urban[p['id']],p['area']))['id']
    cul,rel,_=COUNTRY[o]
    sh=SHARES[income(o)]; tot=sum(sh)
    out=''
    sizes={p['id']:{} for p in mine}
    for p in mine:
        share=(r*p['area']/sa+(1-r)*urban[p['id']]/su) if su>0 else p['area']/sa
        people=max(400.0,total*share); prov_pop[p['id']]=people
        for t,s in zip(TYPES,sh):
            # without a mine in the province the game files labourers under farmers
            t2='farmers' if t=='labourers' else t
            home=p['state_capital'] if t in CAPITAL_ONLY else p['id']
            sizes[home][t2]=sizes[home].get(t2,0.0)+people/SCALE*s/tot
    ALL_SIZES.update(sizes)
    for p in mine:
        out+='%d = {\n'%p['id']
        roles=set()
        sz=sizes[p['id']]
        for t in list(sz):
            if sz[t]<25.0 and t not in ('farmers','clerks'):
                major='farmers' if ROLE.get(t)=='peasant' else 'clerks'
                if t=='aristocrats': continue
                sz[major]=sz.get(major,0.0)+sz.pop(t)
        if sz.get('aristocrats',0.0)<25.0 and 'aristocrats' in sz: sz['clerks']=sz.get('clerks',0.0)+sz.pop('aristocrats')
        for t in ('farmers','clerks'):
            if 0.0<sz.get(t,0.0)<25.0: sz[t]=25.0
        for t in TYPES:
            size=int(round(sizes[p['id']].get(t,0.0)))
            if size<1: continue
            out+='\t%s = { culture = %s religion = %s size = %d }\n'%(t,cul,rel,size)
            roles.add(ROLE.get(t,'urban'))
        out+='}\n'
        for role in sorted(roles):
            households.append((p['id'],role))
    pops_txt[o]=out
for o,t in pops_txt.items(): w('history/pops/2010.1.1/%s.txt'%o,t)

# ---------- countries ----------
gov_allowed={}
g=rd('common/governments.txt')
for m in re.finditer(r'\n(\w+)\s*=\s*\{([^}]*)\}',g,re.S):
    gov_allowed[m.group(1)]=[a for a,b in re.findall(r'(\w+)\s*=\s*(yes|no)',m.group(2)) if b=='yes' and a not in ('election','appoint_ruling_party')]
ideologies=['fascist','reactionary','conservative','socialist','communist','liberal','anarcho_liberal']
PREFER={'democracy':['liberal','conservative','socialist'],'hms_government':['conservative','liberal'],'absolute_monarchy':['reactionary','conservative'],'presidential_dictatorship':['reactionary','conservative','fascist'],'proletarian_dictatorship':['communist','socialist']}
POLICY={'fascist':('state_capitalism','protectionism','moralism','residency','jingoism'),'reactionary':('interventionism','protectionism','moralism','residency','pro_military'),
 'conservative':('interventionism','protectionism','pluralism','limited_citizenship','pro_military'),'socialist':('state_capitalism','free_trade','secularized','full_citizenship','anti_military'),
 'communist':('planned_economy','protectionism','pro_atheism','full_citizenship','pro_military'),'liberal':('laissez_faire','free_trade','pluralism','limited_citizenship','anti_military'),
 'anarcho_liberal':('laissez_faire','free_trade','secularized','full_citizenship','pacifism')}
issues=rd('common/issues.txt')
def groups_of(section):
    m=re.search(section+r'\s*=\s*\{',issues); i=m.end(); depth=1; start=i
    while depth:
        c=issues[i]; depth+= (c=='{') - (c=='}'); i+=1
    body=issues[start:i-1]; out=[]; j=0
    for gm in re.finditer(r'\n\t(\w+)\s*=\s*\{',body):
        k=gm.end(); depth=1
        while depth:
            c=body[k]; depth+=(c=='{')-(c=='}'); k+=1
        opts=re.findall(r'\n\t\t(\w+)\s*=\s*\{',body[gm.end():k])
        if opts: out.append((gm.group(1),opts))
    return out
political=groups_of('political_reforms'); social=groups_of('social_reforms')
techs=[]
for f in sorted(glob.glob(V2+'technologies/*.txt')):
    techs+=re.findall(r'\n(\w+)\s*=\s*\{\s*\n\s*area',re.sub(r'#.*','','\n'+open(f,encoding='latin-1').read()))
base_countries=open(V2+'common/countries.txt',encoding='latin-1').read()
reb=re.search(r'REB\s*=\s*"[^"]*"',base_countries).group(0)
ctxt=reb+'\n'
ranked=sorted(owners,key=gdp,reverse=True)
# ---------- map colours ----------
# The large and familiar countries keep the colour a reader expects of them;
# every other country takes the hue that differs most from its neighbours, so
# that no border separates two similar colours. Saturation and value stay in a
# middle band: the map shader models them with relief and deepens them at the
# frontier, which pure or pale colours would not survive.
FIXED_COLOUR={'USA':(66,104,158),'CAN':(158,74,70),'MEX':(92,146,102),'BRA':(84,154,92),'ARG':(132,174,206),'GBR':(180,76,88),'FRA':(64,102,176),
 'DEU':(118,122,130),'ITA':(98,164,110),'ESP':(206,170,78),'PRT':(64,126,96),'RUS':(74,128,94),'CHN':(190,90,76),'IND':(206,150,90),'JPN':(196,140,156),
 'KOR':(100,152,192),'PRK':(152,76,76),'TUR':(166,176,122),'IRN':(100,154,144),'SAU':(124,162,106),'EGY':(194,174,124),'ZAF':(152,124,172),
 'AUS':(92,162,152),'IDN':(188,114,96),'PAK':(86,134,106),'UKR':(102,152,200),'POL':(196,98,118),'KAZ':(114,170,180),'NGA':(114,162,96),
 'SWE':(86,134,190),'NOR':(128,98,110),'FIN':(140,166,204),'DZA':(140,170,150),'COD':(96,120,170),'MNG':(178,150,110)}
own_lut=np.zeros(int(pid.max())+1,dtype=np.int32)
tag_index={o:i+1 for i,o in enumerate(owners)}
for p in P: own_lut[p['id']]=tag_index[p['owner']]
own_map=own_lut[pid]
neighbours={o:set() for o in owners}
for a,b in ((own_map[:,:-1],own_map[:,1:]),(own_map[:-1,:],own_map[1:,:])):
    m=(a!=b)&(a>0)&(b>0)
    for x,y in set(zip(a[m].tolist(),b[m].tolist())):
        neighbours[owners[x-1]].add(owners[y-1]); neighbours[owners[y-1]].add(owners[x-1])
area_of={o:0.0 for o in owners}
for p in P: area_of[p['owner']]+=p['area']
def hue_of(rgb): return colorsys.rgb_to_hsv(rgb[0]/255.0,rgb[1]/255.0,rgb[2]/255.0)[0]*360.0
MAP_COLOUR=dict(FIXED_COLOUR); hues={o:hue_of(c) for o,c in FIXED_COLOUR.items() if o in neighbours}
for o in sorted(owners,key=lambda t:-area_of[t]):
    if o in MAP_COLOUR: continue
    h=int(hashlib.md5(o.encode()).hexdigest()[:8],16)
    taken=[hues[n] for n in neighbours[o] if n in hues]
    def clearance(c): return min([min(abs(c-t),360-abs(c-t)) for t in taken] or [180])
    cands=[(k*20+(h%20))%360 for k in range(18)]
    best=max(cands,key=lambda c:(min(clearance(c),70),-((c*7+h)%360)))
    hues[o]=best
    r_,g_,b_=colorsys.hsv_to_rgb(best/360.0,0.36+((h>>9)%20)/100.0,0.58+((h>>15)%18)/100.0)
    MAP_COLOUR[o]=(int(r_*255),int(g_*255),int(b_*255))
loc=[]
def loc_add(k,v): loc.append('%s;%s;;;;;;;;;;;;;x'%(k,ascii_(v)))
# Relations and diplomats on the first day: a hidden decision per country, named in its history.
import diplomacy_2010
START_LINES,N_RELATIONS=diplomacy_2010.write(w,loc_add,owners)
for o in owners:
    cul,rel,gcode=COUNTRY[o]; gov=GOVERNMENT[gcode]
    body='color = { %d %d %d }\ngraphical_culture = EuropeanGC\n'%MAP_COLOUR[o]
    for idl in ideologies:
        e,t,rp,cp,wp=POLICY[idl]
        body+='party = {\n\tname = "%s_%s"\n\tstart_date = 1990.1.1\n\tend_date = 2200.1.1\n\tideology = %s\n\teconomic_policy = %s\n\ttrade_policy = %s\n\treligious_policy = %s\n\tcitizenship_policy = %s\n\twar_policy = %s\n}\n'%(o,idl,idl,e,t,rp,cp,wp)
        loc_add('%s_%s'%(o,idl),'%s %s Party'%(cname(o),idl.replace('_',' ').title()))
    w('common/countries/k2010_%s.txt'%o,body)
    ctxt+='%s = "countries/k2010_%s.txt"\n'%(o,o)
    allowed=gov_allowed.get(gov,ideologies)
    ruling=next((i for i in PREFER[gov] if i in allowed),allowed[0] if allowed else 'conservative')
    free=gcode in ('D','M')
    lvl=income(o)
    hist='capital = %d\nprimary_culture = %s\nreligion = %s\ngovernment = %s\nplurality = %d\nnationalvalue = %s\nliteracy = %.2f\ncivilized = yes\nprestige = %d\n'%(
        capital[o],cul,rel,gov,60 if free else 20,'nv_liberty' if free else 'nv_order',[0.99,0.94,0.78,0.58][lvl],max(1,100-ranked.index(o)))
    for name,opts in political:
        hist+='%s = %s\n'%(name,opts[-1] if (free or name=='slavery') else opts[0])
    for name,opts in social:
        hist+='%s = %s\n'%(name,opts[[len(opts)-1,len(opts)//2,max(0,len(opts)//3),0][lvl]])
    hist+='ruling_party = %s_%s\nupper_house = {\n'%(o,ruling)
    for idl in ideologies: hist+='\t%s = %d\n'%(idl,100 if idl==ruling else 0)
    hist+='}\n'
    for t in techs: hist+='%s = 1\n'%t
    hist+=START_LINES[o]
    w('history/countries/%s - %s.txt'%(o,cname(o)),hist)
    loc_add(o,cname(o)); loc_add(o+'_ADJ',cname(o))
w('common/countries.txt',ctxt)
for p in P: loc_add('PROV%d'%p['id'],p['name'])
for k,name in state_keys: loc_add(k,name)
# ---------- cities ----------
# Places for the map: every national capital, then the large cities, each at
# its own position on the game map. The interface shows them by scale.
city_rows=[]; seen_city=set()
for f in sorted(places,key=lambda f:-float(f['properties'].get('pop_max') or 0)):
    pr=f['properties']
    is_cap=pr['featurecla'].startswith('Admin-0 capital')
    pop=float(pr.get('pop_max') or 0)
    if not is_cap and pop<750000 and int(pr.get('scalerank') or 9)>3: continue
    x,y=geo.to_pixel(np.array([pr['longitude']]),np.array([pr['latitude']]))
    xf=float(x[0])%W; yf=float(y[0]); xi=int(round(xf))%W; yi=int(round(yf))
    if not (0<=yi<H): continue
    i=int(pid[yi,xi])
    if i==0:
        ys,xs=np.nonzero(pid[max(0,yi-6):yi+7,max(0,xi-6):min(W,xi+7)])
        if len(ys)==0: continue
        k=np.argmin((ys-6)**2+(xs-6)**2); i=int(pid[max(0,yi-6)+ys[k],max(0,xi-6)+xs[k]])
    name=ascii_(pr.get('nameascii') or pr.get('name') or '')
    if not name or (name,i) in seen_city: continue
    seen_city.add((name,i))
    city_rows.append('%d;%.1f;%.1f;%d;%d;%s'%(i,xf,yf,1 if is_cap else 0,int(pop),name))
w('map/katechon_cities.csv','province;x;y;capital;population;name\n'+'\n'.join(city_rows)+'\n')
# ---------- province areas ----------
# Square kilometres of each province, from its pixel count and the local scale
# of the map at its centre. The interface needs it for population density.
area_rows=[]
for p in P:
    lat=float(p['lat'] if p['lat'] is not None else 0.0); lon=float(p['lon'] if p['lon'] is not None else 0.0)
    lat=max(-58.0,min(86.0,lat))
    x0,y0=geo.to_pixel(np.array([lon]),np.array([lat])); x1,_=geo.to_pixel(np.array([lon+0.5]),np.array([lat])); _,y1=geo.to_pixel(np.array([lon]),np.array([lat+0.5]))
    px_per_deg_x=max(1e-3,abs(float(x1[0]-x0[0]))/0.5); px_per_deg_y=max(1e-3,abs(float(y1[0]-y0[0]))/0.5)
    km2_per_px=(111.32*np.cos(np.radians(lat))/px_per_deg_x)*(110.57/px_per_deg_y)
    area_rows.append('%d;%.1f'%(p['id'],max(1.0,p['area']*km2_per_px)))
w('map/katechon_provinces.csv','province;area_km2\n'+'\n'.join(area_rows)+'\n')
# One unit of population in the game stands for this many people.
loc_add('katechon_people_per_unit','%d'%int(SCALE))
loc_add('katechon_people_per_pop_unit',str(int(SCALE))); loc_add('KATECHON_2010_NAME','The World in 2010'); loc_add('KATECHON_2010_DESC','The first day of 2010.')
w('localisation/katechon2010.csv','\n'.join(loc)+'\n')
for d in ('history/units','history/wars','events','decisions'):
    w(d+'/empty.txt','')

# ---------- diplomacy on 1 January 2010 ----------
# Mutual-defence treaties in force that day. Blocs are written as every pair of members,
# because the game knows alliances only between two states.
BLOCS={'NATO':'USA CAN GBR FRA DEU ITA ESP PRT NLD BEL LUX DNK NOR ISL GRC TUR POL CZE HUN SVK SVN EST LVA LTU ROU BGR HRV ALB'.split(),
       'CSTO':'RUS BLR KAZ ARM KGZ TJK UZB'.split()}
PAIRS=[('USA','JPN'),('USA','KOR'),('USA','AUS'),('AUS','NZL'),('USA','PHL'),('USA','THA'),('CHN','PRK')]
pairs=set()
for members in BLOCS.values():
    live=[m for m in members if m in owners]
    for i,x in enumerate(live):
        for y in live[i+1:]: pairs.add((x,y))
for x,y in PAIRS:
    if x in owners and y in owners: pairs.add((x,y))
w('history/diplomacy/Alliances.txt',''.join('alliance = {\n\tfirst = %s\n\tsecond = %s\n\tstart_date = 2010.1.1\n\tend_date = 2100.1.1\n}\n'%p for p in sorted(pairs)))
for name in ('Guarantees','PuppetStates','Unions'): w('history/diplomacy/%s.txt'%name,'')
N_ALLIANCES=len(pairs)
# What each economy produced in 2010, in US dollars (World Bank): how governments
# weigh one another's economic power before their own reports come in.
w('common/canonical_runtime/national_accounts.csv','country;gdp_usd\n'+''.join('%s;%.0f\n'%(o,gdp(o)) for o in owners))
w('common/bookmarks.txt','bookmark = {\n\tname = "KATECHON_2010_NAME"\n\tdesc = "KATECHON_2010_DESC"\n\tdate = 2010.1.1\n\tcameraX = 2950\n\tcameraY = 1550\n}\n')
defines=open(V2+'common/defines.lua',encoding='latin-1').read()
defines=re.sub(r"start_date\s*=\s*'[^']*'","start_date = '2010.1.1'",defines); defines=re.sub(r"end_date\s*=\s*'[^']*'","end_date = '2100.12.31'",defines)
# Scenario creation pre-simulates the economy; each day is costly at world scale.
defines=defines.replace("start_date = '2010.1.1'","start_date = '2010.1.1',\n\tALICE_ECONOMY_PRESIM_DAYS = %d"%int(os.environ.get('KATECHON_PRESIM_DAYS','3')),1)
defines=re.sub(r"BASE_MONTHLY_DIPLOPOINTS\s*=\s*[0-9.]+","BASE_MONTHLY_DIPLOPOINTS = %.1f"%diplomacy_2010.MONTHLY_DIPLOMATS,defines)
w('common/defines.lua',defines)

BUILDINGS={}
# ---------- canonical runtime tables ----------
HEAD={'firms':'firm_id;kind;settlement;opening_cash;retained_earnings;paid_in_equity','capital_owners':'owner_id;kind;reference;settlement;opening_cash',
'assets':'asset_id;kind;site_id;operator_id;province_id;building;ordinal;commodity_id;opening_value','ownership':'asset_id;owner_type;owner_id;ownership;voting;economic',
'loans':'loan_id;asset_id;creditor_type;creditor_id;principal;annual_rate;creation_date;due_date;accrued_interest;collateral_value',
'banks':'bank_id;jurisdiction;settlement;opening_reserves;opening_equity;opening_deposit_liabilities;lending_base_rate;minimum_capital_ratio;liquidity_target;risk_appetite;lending_spread;max_single_borrower_exposure;reserve_requirement;capital_breach_grace_days',
'bank_deposits':'deposit_id;bank_id;owner_type;owner_id;opening_balance','deposits':'province_id;commodity_id;original_reserves;remaining_reserves;grade;daily_capacity',
'land_titles':'title_id;site_id;province_id;area_hectares;suitability;opening_value','farms':'farm_id;title_id;production_type;operator_type;operator_id;opening_value',
'leases':'lease_id;title_id;tenant_type;tenant_id;settlement;cash_rent_per_hectare_year;output_share;valid_from;valid_until','households':'household_id;province_id;role;settlement;opening_cash',
'workshops':'workshop_id;province_id;production_type;operator_type;operator_id;opening_value','military_equipment':'equipment_id;category;commodity;mass;reliability;attack;defense;range_km',
'formation_templates':'template_id;personnel_authorization','formation_template_equipment':'template_id;equipment_id;quantity',
'formation_template_consumables':'template_id;consumable;commodity;per_person;per_equipment_tonne',
'military_formations':'formation_id;parent_formation_id;template_id;owner_tag;province_id;status;operational_tempo;legacy_regiment_index',
'formation_personnel':'formation_id;source_population_cell;first_ordinal;count;ordinal_stride;training_days','formation_equipment':'formation_id;equipment_id;quantity',
'formation_consumables':'formation_id;consumable;quantity','military_stockpiles':'stockpile_id;owner_tag;province_id;stockpile_kind;cargo_kind;equipment_id;consumable;quantity',
'formation_labels':'formation_id;echelon;name_key;opening_personnel'}

# ---------- the opening economy ----------
MONEY='precious_metal'
MAX_CROPS=int(os.environ.get('KATECHON_MAX_CROPS','1'))
MAX_CRAFTS=int(os.environ.get('KATECHON_MAX_CRAFTS','1'))
# Game money per US dollar at model scale: roughly 0.01 per person-year of subsistence.
USD=float(os.environ.get('KATECHON_MONEY_PER_USD','1e-5'))/PEOPLE_PER_PERSON
rows={'households':[],'firms':[],'capital_owners':[],'assets':[],'ownership':[],'deposits':[],'land_titles':[],'farms':[],'workshops':[],'banks':[],'bank_deposits':[]}
def f(x): return ('%.6f'%x).rstrip('0').rstrip('.')
CROPS_ALL=[('grain','grain_farm','grain',0.42),('cattle','cattle_ranch','cattle',0.18),('fruit','orchard','fruit',0.10),('wool','sheep_ranch','wool',0.08),('timber','timber_lodge','timber',0.08),('fish','fishing_wharf','fish',0.08)]
CROPS_TROPIC=[('coffee','coffee_plantation','coffee',0.04),('cotton','cotton_plantation','cotton',0.05),('rubber','rubber_lodge','rubber',0.03)]
CROPS_WARM=[('cotton','cotton_plantation','cotton',0.05),('tobacco','tobacco_plantation','tobacco',0.03),('tea','tea_plantation','tea',0.03)]
CRAFTS=['artisan_regular_clothes','artisan_furniture','artisan_liquor','artisan_canned_food','artisan_paper','artisan_glass','artisan_lumber','artisan_fabric','artisan_winery','artisan_cement']
FACTORIES={0:['machine_parts_factory','electric_gear_factory','automobile_factory','telephone_factory','radio_factory','fuel_refinery','steel_factory','luxury_clothes_factory','luxury_furniture_factory','regular_clothes_factory','furniture_factory','paper_mill','glass_factory','cement_factory','fertilizer_factory','canned_food_factory','fabric_factory','lumber_mill'],
 1:['steel_factory','machine_parts_factory','fuel_refinery','cement_factory','regular_clothes_factory','furniture_factory','fabric_factory','lumber_mill','glass_factory','paper_mill','fertilizer_factory','canned_food_factory','electric_gear_factory','automobile_factory'],
 2:['regular_clothes_factory','fabric_factory','cement_factory','lumber_mill','furniture_factory','steel_factory','canned_food_factory','glass_factory','fertilizer_factory'],
 3:['regular_clothes_factory','fabric_factory','lumber_mill','cement_factory','canned_food_factory']}
# Share of world output around 2010, for the minerals the content knows.
MINES={'coal':('coal_mine',{'CHN':47,'USA':14,'IND':8,'AUS':6,'RUS':4.5,'IDN':4.5,'ZAF':3.5,'DEU':2.5,'POL':2,'KAZ':1.5,'UKR':1,'COL':1,'CAN':1,'VNM':0.6,'GBR':0.3,'CZE':0.8,'TUR':1,'MNG':0.3}),
 'iron':('iron_mine',{'CHN':38,'AUS':16,'BRA':14,'IND':8.5,'RUS':4,'UKR':3,'ZAF':2.2,'USA':1.9,'CAN':1.4,'IRN':1.3,'SWE':1,'KAZ':0.9,'VEN':0.6,'MEX':0.5,'MRT':0.4}),
 'oil':('oil_rig',{'RUS':12.6,'SAU':12,'USA':8.7,'IRN':5.2,'CHN':5,'CAN':4.1,'MEX':3.6,'ARE':3.3,'VEN':3.2,'KWT':3,'IRQ':3,'NGA':2.9,'BRA':2.7,'NOR':2.5,'AGO':2.3,'KAZ':2.1,'DZA':2,'LBY':2,'QAT':1.7,'GBR':1.6,'AZE':1.3,'IDN':1.2,'IND':1,'OMN':1,'EGY':0.9,'ARG':0.8,'COL':1,'MYS':0.9,'ECU':0.6,'SDN':0.6,'SYR':0.5}),
 'sulphur':('sulphur_mine',{'CHN':14,'USA':13,'CAN':10,'RUS':10,'DEU':5.5,'SAU':4.7,'JPN':4.7,'KAZ':4,'ARE':2.9,'MEX':2.5,'IRN':2.3,'CHL':2.5,'POL':1.7,'IND':1.6,'KOR':2.5})}
world_persons=sum(prov_pop.values())/PEOPLE_PER_PERSON
bank_total={}
for o in owners:
    mine=[p for p in P if p['owner']==o]
    g=gdp(o)*USD
    people=sum(prov_pop[p['id']] for p in mine)
    cap=capital[o]
    gov='gov_'+o
    rows['capital_owners'].append('%s;government;%s;%s;%s'%(gov,o,MONEY,f(0.10*g)))
    # households: a month and a half of income in hand, more for the propertied
    weights={}
    for p in mine:
        sz=ALL_SIZES[p['id']]
        for role in ('peasant','landed','urban'):
            n=sum(v for t,v in sz.items() if ROLE.get(t,'urban')==role and round(v)>=1)
            if n>0: weights[(p['id'],role)]=n*{'peasant':0.5,'landed':12.0,'urban':1.5}[role]
    tw=sum(weights.values()) or 1.0
    for (i,role),wt in sorted(weights.items()):
        rows['households'].append('hh_%d_%s;%d;%s;%s;%s'%(i,role,i,role,MONEY,f(0.12*g*wt/tw)))
    for p in mine:
        i=p['id']; sz=ALL_SIZES[i]; site='site_%d'%i
        peasants=4.0*sum(v for t,v in sz.items() if ROLE.get(t)=='peasant')
        if ('hh_%d_peasant'%i) in {r.split(';')[0] for r in rows['households'][-len(weights)-1:]} or (i,'peasant') in weights:
            lat=abs(p['lat'] if p['lat'] is not None else 40.0)
            crops=list(CROPS_ALL)+(CROPS_TROPIC if lat<24 else CROPS_WARM if lat<42 else [])
            if MAX_CROPS<len(crops):
                # Order matching in the engine scales with the square of the number of sellers, so a
                # province specialises: its crops are drawn from the regional list in proportion to their weights.
                wheel=[c for c in crops for _ in range(max(1,int(round(c[3]*100))))]
                picks=[]
                for k in range(MAX_CROPS*7):
                    c=wheel[(i*7919+k*104729)%len(wheel)]
                    if c not in picks: picks.append(c)
                    if len(picks)==MAX_CROPS: break
                crops=picks
            tot=sum(c[3] for c in crops)
            workers=max(1.0,peasants*0.45)
            for key,ptype,com,wgt in crops:
                area=max(5.0,workers*5.0*wgt/tot)
                value=area*0.02
                rows['land_titles'].append('lt_%d_%s;%s;%d;%s;%s=1;%s'%(i,key,site,i,f(area),com,f(value)))
                rows['farms'].append('fm_%d_%s;lt_%d_%s;%s;household;hh_%d_peasant;%s'%(i,key,i,key,ptype,i,f(value*0.25)))
                rows['ownership'].append('lt_%d_%s;household;hh_%d_peasant;1;1;1'%(i,key,i))
                rows['ownership'].append('fm_%d_%s;household;hh_%d_peasant;1;1;1'%(i,key,i))
        if (i,'urban') in weights:
            urban_persons=4.0*sum(v for t,v in sz.items() if ROLE.get(t,'urban')=='urban')
            # the engine holds at most 50,000 assets in all, so workshops are kept to the staple crafts
            for c in [CRAFTS[(i*31+k)%len(CRAFTS)] for k in range(min(MAX_CRAFTS,3 if urban_persons<400 else 5 if urban_persons<4000 else 7))]:
                rows['workshops'].append('ws_%d_%s;%d;%s;household;hh_%d_urban;%s'%(i,c,i,c,i,f(max(1.0,urban_persons*0.002))))
                rows['ownership'].append('ws_%d_%s;household;hh_%d_urban;1;1;1'%(i,c,i))
    deposits_cash=[('gov',gov,0.05*g)]
    # mining: one national resources company holding the country's deposits
    mined=[(com,spec[0],spec[1][o]) for com,spec in MINES.items() if o in spec[1]]
    if mined:
        firm=o+'_resources'
        rows['firms'].append('%s;state_entity;%s;%s;0;%s'%(firm,MONEY,f(0.01*g),f(0.01*g)))
        rows['ownership'].append('equity:%s;capital_owner;%s;1;1;1'%(firm,gov))
        deposits_cash.append(('firm',firm,0.005*g))
        big=sorted(mine,key=lambda p:-p['area'])
        for n,(com,ptype,share) in enumerate(mined):
            p=big[n%len(big)]; i=p['id']
            capacity=max(0.5,world_persons*share/100.0*0.004)
            rows['deposits'].append('%d;%s;%s;%s;1;%s'%(i,com,f(capacity*365*80),f(capacity*365*80),f(capacity)))
            rows['assets'].append('dp_%d_%s;deposit;site_%d;%s;%d;;;%s;%s'%(i,com,i,firm,i,com,f(capacity*40)))
            rows['assets'].append('xp_%d_%s;extraction;site_%d;%s;%d;%s;;%s;%s'%(i,com,i,firm,i,ptype,com,f(capacity*10)))
            rows['ownership'].append('dp_%d_%s;firm;%s;1;1;1'%(i,com,firm))
            rows['ownership'].append('xp_%d_%s;firm;%s;1;1;1'%(i,com,firm))
    # manufacturing: one national industrial company, plants where the craftsmen live
    plants=[]
    kinds=FACTORIES[income(o)]
    k=0
    for p in sorted(mine,key=lambda p:-ALL_SIZES[p['id']].get('craftsmen',0.0)):
        craftsmen=ALL_SIZES[p['id']].get('craftsmen',0.0)
        count=int(min(4,craftsmen//600))
        for _ in range(count):
            if k>=len(kinds)*2: break
            plants.append((p['id'],kinds[k%len(kinds)])); k+=1
    if not plants and ALL_SIZES[cap].get('craftsmen',0.0)>=60: plants.append((cap,kinds[0]))
    BUILDINGS.update({})
    if plants:
        firm=o+'_industry'
        rows['firms'].append('%s;company;%s;%s;0;%s'%(firm,MONEY,f(0.02*g),f(0.02*g)))
        holder=('household','hh_%d_urban'%cap) if (cap,'urban') in weights else ('capital_owner',gov)
        if holder[0]=='household':
            rows['ownership'].append('equity:%s;household;%s;0.7;0.7;0.7'%(firm,holder[1]))
            rows['ownership'].append('equity:%s;capital_owner;%s;0.3;0.3;0.3'%(firm,gov))
        else:
            rows['ownership'].append('equity:%s;capital_owner;%s;1;1;1'%(firm,gov))
        deposits_cash.append(('firm',firm,0.01*g))
        seen={}
        for i,b in plants:
            n=seen.get((i,b),0)+1; seen[(i,b)]=n
            BUILDINGS.setdefault(i,[]).append(b)
            aid='fa_%d_%s_%d'%(i,b,n)
            rows['assets'].append('%s;factory;site_%d;%s;%d;%s;%d;;%s'%(aid,i,firm,i,b,n,f(0.004*g/len(plants))))
            rows['ownership'].append('%s;firm;%s;1;1;1'%(aid,firm))
    # one national bank holding the treasury's and the companies' deposits
    bank=o+'_bank'
    # The loader re-derives equity from the written figures, so compute it from the same rounded values.
    deposits_cash=[(k,i,float(f(a))) for k,i,a in deposits_cash]
    total=float(f(sum(x[2] for x in deposits_cash)))
    reserves=float(f(total*1.2)); equity=float(f(reserves-total))
    rows['firms'].append('%s;bank;%s;0;0;%s'%(bank,MONEY,f(equity)))
    rows['ownership'].append('equity:%s;capital_owner;%s;1;1;1'%(bank,gov))
    rows['banks'].append('%s;%s;%s;%s;%s;%s;0.04;0.08;0.2;0.5;0.03;0.25;0.1;90'%(bank,o,MONEY,f(reserves),f(equity),f(total)))
    for kind,owner_id,amount in deposits_cash:
        rows['bank_deposits'].append('bd_%s_%s;%s;%s;%s;%s'%(o,owner_id,bank,'capital_owner' if kind=='gov' else 'firm',owner_id,f(amount)))
for p in P:
    o=p['owner']
    body='owner = %s\ncontroller = %s\nadd_core = %s\ntrade_goods = grain\nlife_rating = 35\n'%(o,o,o)
    for b in BUILDINGS.get(p['id'],[]):
        body+='state_building = {\n\tlevel = 1\n\tbuilding = %s\n\tupgrade = yes\n}\n'%b
    w('history/provinces/k2010/%d - %s.txt'%(p['id'],re.sub(r'[^A-Za-z0-9 ]','',ascii_(p['name']))[:40]),body)

# ---------- the land forces of 2010 ----------
import forces_2010
force_rows,force_en,force_ru,force_missing=forces_2010.build(P,capital,prov_pop,PEOPLE_PER_PERSON,cname)
rows.update(force_rows)
if force_missing: print('land forces: no province for',force_missing)
w('localisation/katechon2010_forces.csv',''.join('%s;%s;;;;;;;;;;;;;x\n'%(k,ascii_(v)) for k,v in force_en))
# The Russian names go to the game's own Russian text, beside the HUD's.
ru_dir=os.environ.get('KATECHON_REPO',os.path.join('..','..'))+'/assets/localisation/ru-RU'
if os.path.isdir(ru_dir): open(ru_dir+'/katechon_forces_2010.csv','w',encoding='utf-8').write(''.join('%s;%s\n'%kv for kv in force_ru))
print('land forces:',len(force_rows['military_formations']),'formations in',len({r.split(';')[3] for r in force_rows['military_formations']}),'armies')

for name,head in HEAD.items():
    w('common/canonical_runtime/%s.csv'%name,head+'\n'+''.join(r+'\n' for r in rows.get(name,[])),'utf-8')
open(V2+'mod/%s.mod'%MOD,'w').write('name = "KATECHON 2010"\npath = "mod/%s"\nreplace_path = "history/provinces"\nreplace_path = "history/countries"\nreplace_path = "history/pops"\nreplace_path = "history/units"\nreplace_path = "history/diplomacy"\nreplace_path = "history/wars"\nreplace_path = "events"\nreplace_path = "decisions"\n'%MOD)
json.dump({'capital':capital,'prov_pop':prov_pop,'states':states},open('mod_summary.json','w'))
print({k:len(v) for k,v in rows.items()},'alliances',N_ALLIANCES,'relations',N_RELATIONS)
print('states',len(states),'households',len(households),'world population',sum(prov_pop.values())/1e9,'bn; techs',len(techs),'reform groups',len(political),len(social))
