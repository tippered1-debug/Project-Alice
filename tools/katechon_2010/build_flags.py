"""Stage 5: the flags of the states of 2010, as gfx/flags/<TAG>*.png in the mod.

The base game's flags are those of the nineteenth century. These are today's
national flags from flagcdn.com (public domain), with the ones that differed on
1 January 2010 drawn here. One picture serves every form of government.
"""
import json, os, urllib.request
from PIL import Image
V2='/Users/stefanbidzamov/Games/Victoria2/'
OUT=V2+'mod/katechon2010/gfx/flags/'
SIZE=(93,64)
# every form of government names the flag it flies; all of them get the same picture
import re, glob
FORMS=['']+sorted({'_'+m for f in [V2+'common/governments.txt']+glob.glob(V2+'mod/katechon2010/common/governments.txt')
                   for m in re.findall(r'flagType\s*=\s*(\w+)',open(f,encoding='latin-1').read())})
ISO={'FRA':'fr','NOR':'no','KOS':'xk','TWN':'tw','ADR':'ad'}
# as they were on 1 January 2010
def libya(): return Image.new('RGB',SIZE,(35,158,70))
THEN={'LBY':libya}
d=json.load(open('provinces.json'))
owners=sorted({p['owner'] for p in d['provinces']})
os.makedirs(OUT,exist_ok=True); os.makedirs('flags',exist_ok=True)
done=0; missing=[]
for tag in owners:
    if tag in THEN: im=THEN[tag]()
    else:
        code=ISO.get(tag) or (d['info'].get(tag,{}).get('ISO_A2') or '').lower()
        if len(code)!=2: missing.append(tag); continue
        cache='flags/%s.png'%code
        if not os.path.exists(cache):
            try:
                req=urllib.request.Request('https://flagcdn.com/w160/%s.png'%code,headers={'User-Agent':'katechon-build'})
                open(cache,'wb').write(urllib.request.urlopen(req,timeout=30).read())
            except Exception: missing.append(tag); continue
        im=Image.open(cache).convert('RGB').resize(SIZE,Image.LANCZOS)
    im=im.convert('RGBA')
    for form in FORMS: im.save(OUT+tag+form+'.png')
    done+=1
print('flags',done,'of',len(owners),'missing',missing)
