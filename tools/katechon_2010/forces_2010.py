# Land forces on 1 January 2010, for the canonical land-force tables.
#
# The chain of command is what each army really had that day, as far as it is
# written down here: Russia by military district, army and brigade; the United
# States by corps and division; China by military region and group army; India
# by regional command. Every other army listed is one formation, because its
# structure is not authored yet. Strengths and tank parks are rounded figures
# of the kind the Military Balance prints, not a count.
#
# One simulated person stands for PEOPLE_PER_PERSON people, so a brigade of
# four and a half thousand is one person. Equipment is counted in real pieces.
# The engine allows one equipment model per commodity, so the models are
# classes of equipment, not types: a tank, an armoured vehicle, a gun.

import unicodedata

EQUIPMENT = [  # id, category, commodity, mass kg, reliability, attack, defense, range km
    ('tank', 1, 'barrels', 46000, 0.9, 10, 8, 4),
    ('armoured_vehicle', 2, 'automobiles', 14000, 0.9, 4, 4, 2),
    ('gun', 3, 'artillery', 25000, 0.9, 8, 2, 25),
    ('small_arms', 4, 'small_arms', 4, 0.95, 1, 1, 0.5),
]
MASS = {e[0]: e[3] for e in EQUIPMENT}
# What the HUD calls each category of equipment (katechon_mil_eq_<category>).
EQUIPMENT_NAMES = {1: ('Tanks', 'Танки'), 2: ('Armoured vehicles', 'Бронемашины'), 3: ('Artillery', 'Артиллерия'), 4: ('Small arms', 'Стрелковое оружие')}
# Per simulated person and per tonne of equipment a day, in goods units of the
# scenario economy. Not calibrated against it yet.
CONSUMABLES = [('food', 'canned_food', 0.004, 0.0), ('fuel', 'fuel', 0.0, 0.00001), ('ammunition', 'ammunition', 0.0, 0.000004)]

# template: (people, {equipment: pieces})
TEMPLATES = {
    'command': (5000, {'armoured_vehicle': 60, 'small_arms': 3000}),
    'army_troops': (5000, {'gun': 54, 'armoured_vehicle': 90, 'small_arms': 4000}),
    'corps_troops': (10000, {'gun': 90, 'armoured_vehicle': 300, 'small_arms': 9000}),
    'motor_rifle_brigade': (4500, {'tank': 41, 'armoured_vehicle': 160, 'gun': 54, 'small_arms': 4000}),
    'tank_brigade': (3000, {'tank': 94, 'armoured_vehicle': 60, 'gun': 26, 'small_arms': 2500}),
    'light_brigade': (3500, {'armoured_vehicle': 110, 'gun': 24, 'small_arms': 3200}),
    'heavy_division': (20000, {'tank': 230, 'armoured_vehicle': 700, 'gun': 100, 'small_arms': 18000}),
    'light_division': (17000, {'armoured_vehicle': 350, 'gun': 70, 'small_arms': 16000}),
    'group_army': (50000, {'tank': 350, 'armoured_vehicle': 600, 'gun': 400, 'small_arms': 45000}),
}

# id, echelon, template, province name, parent, English name, Russian name
RUS = [
    ('len', 'theatre', 'command', 'Leningrad', '', 'Leningrad Military District', 'Ленинградский военный округ'),
    ('138b', 'brigade', 'motor_rifle_brigade', 'Leningrad', 'len', '138th Guards Motor Rifle Brigade', '138-я гв. мотострелковая бригада'),
    ('25b', 'brigade', 'motor_rifle_brigade', 'Pskov', 'len', '25th Guards Motor Rifle Brigade', '25-я гв. мотострелковая бригада'),
    ('200b', 'brigade', 'motor_rifle_brigade', 'Murmansk', 'len', '200th Motor Rifle Brigade', '200-я мотострелковая бригада'),

    ('mos', 'theatre', 'command', 'Moscow', '', 'Moscow Military District', 'Московский военный округ'),
    ('20a', 'army', 'army_troops', 'Voronezh', 'mos', '20th Guards Army', '20-я гв. общевойсковая армия'),
    ('4tb', 'brigade', 'tank_brigade', 'Moscow', '20a', '4th Guards Tank Brigade', '4-я гв. танковая бригада'),
    ('5b', 'brigade', 'motor_rifle_brigade', 'Moscow', '20a', '5th Guards Motor Rifle Brigade', '5-я гв. мотострелковая бригада'),
    ('22a', 'army', 'army_troops', 'Nizhny Novgorod', 'mos', '22nd Guards Army', '22-я гв. общевойсковая армия'),
    ('6tb', 'brigade', 'tank_brigade', 'Nizhny Novgorod', '22a', '6th Tank Brigade', '6-я танковая бригада'),
    ('9b', 'brigade', 'motor_rifle_brigade', 'Nizhny Novgorod', '22a', '9th Motor Rifle Brigade', '9-я мотострелковая бригада'),
    ('27b', 'brigade', 'motor_rifle_brigade', 'Moscow', 'mos', '27th Guards Motor Rifle Brigade', '27-я гв. мотострелковая бригада'),

    ('cau', 'theatre', 'command', 'Rostov', '', 'North Caucasus Military District', 'Северо-Кавказский военный округ'),
    ('58a', 'army', 'army_troops', 'North Ossetia', 'cau', '58th Army', '58-я общевойсковая армия'),
    ('19b', 'brigade', 'motor_rifle_brigade', 'North Ossetia', '58a', '19th Motor Rifle Brigade', '19-я мотострелковая бригада'),
    ('17b', 'brigade', 'motor_rifle_brigade', 'Chechen', '58a', '17th Guards Motor Rifle Brigade', '17-я гв. мотострелковая бригада'),
    ('18b', 'brigade', 'motor_rifle_brigade', 'Chechen', '58a', '18th Guards Motor Rifle Brigade', '18-я гв. мотострелковая бригада'),
    ('8b', 'brigade', 'light_brigade', 'Chechen', '58a', '8th Guards Mountain Motor Rifle Brigade', '8-я гв. горная мотострелковая бригада'),
    ('136b', 'brigade', 'motor_rifle_brigade', 'Dagestan', '58a', '136th Guards Motor Rifle Brigade', '136-я гв. мотострелковая бригада'),
    ('205b', 'brigade', 'motor_rifle_brigade', 'Stavropol', '58a', '205th Motor Rifle Brigade', '205-я мотострелковая бригада'),
    ('20b', 'brigade', 'motor_rifle_brigade', 'Volgograd', '58a', '20th Guards Motor Rifle Brigade', '20-я гв. мотострелковая бригада'),
    ('33b', 'brigade', 'light_brigade', 'Dagestan', 'cau', '33rd Mountain Motor Rifle Brigade', '33-я горная мотострелковая бригада'),
    ('34b', 'brigade', 'light_brigade', 'KarachayCherkess', 'cau', '34th Mountain Motor Rifle Brigade', '34-я горная мотострелковая бригада'),
    ('56b', 'brigade', 'light_brigade', 'Volgograd', 'cau', '56th Guards Air Assault Brigade', '56-я гв. десантно-штурмовая бригада'),

    ('vol', 'theatre', 'command', 'Sverdlovsk', '', 'Volga-Urals Military District', 'Приволжско-Уральский военный округ'),
    ('2a', 'army', 'army_troops', 'Samara', 'vol', '2nd Guards Army', '2-я гв. общевойсковая армия'),
    ('15b', 'brigade', 'motor_rifle_brigade', 'Samara', '2a', '15th Motor Rifle Brigade', '15-я мотострелковая бригада'),
    ('21b', 'brigade', 'motor_rifle_brigade', 'Orenburg', '2a', '21st Guards Motor Rifle Brigade', '21-я гв. мотострелковая бригада'),
    ('23b', 'brigade', 'motor_rifle_brigade', 'Samara', '2a', '23rd Guards Motor Rifle Brigade', '23-я гв. мотострелковая бригада'),
    ('28b', 'brigade', 'motor_rifle_brigade', 'Sverdlovsk', 'vol', '28th Motor Rifle Brigade', '28-я мотострелковая бригада'),
    ('7tb', 'brigade', 'tank_brigade', 'Chelyabinsk', 'vol', '7th Guards Tank Brigade', '7-я гв. танковая бригада'),

    ('sib', 'theatre', 'command', 'Zabaykalsky', '', 'Siberian Military District', 'Сибирский военный округ'),
    ('41a', 'army', 'army_troops', 'Novosibirsk', 'sib', '41st Army', '41-я общевойсковая армия'),
    ('32b', 'brigade', 'motor_rifle_brigade', 'Novosibirsk', '41a', '32nd Motor Rifle Brigade', '32-я мотострелковая бригада'),
    ('35b', 'brigade', 'motor_rifle_brigade', 'Altai', '41a', '35th Guards Motor Rifle Brigade', '35-я гв. мотострелковая бригада'),
    ('74b', 'brigade', 'motor_rifle_brigade', 'Kemerovo', '41a', '74th Guards Motor Rifle Brigade', '74-я гв. мотострелковая бригада'),
    ('36a', 'army', 'army_troops', 'Zabaykalsky', 'sib', '36th Army', '36-я общевойсковая армия'),
    ('36b', 'brigade', 'motor_rifle_brigade', 'Zabaykalsky', '36a', '36th Guards Motor Rifle Brigade', '36-я гв. мотострелковая бригада'),
    ('5tb', 'brigade', 'tank_brigade', 'Buryatia', '36a', '5th Guards Tank Brigade', '5-я гв. танковая бригада'),
    ('37b', 'brigade', 'motor_rifle_brigade', 'Buryatia', '36a', '37th Guards Motor Rifle Brigade', '37-я гв. мотострелковая бригада'),

    ('far', 'theatre', 'command', 'Khabarovsk', '', 'Far Eastern Military District', 'Дальневосточный военный округ'),
    ('5a', 'army', 'army_troops', 'Primorsky', 'far', '5th Army', '5-я общевойсковая армия'),
    ('57b', 'brigade', 'motor_rifle_brigade', 'Khabarovsk', '5a', '57th Guards Motor Rifle Brigade', '57-я гв. мотострелковая бригада'),
    ('59b', 'brigade', 'motor_rifle_brigade', 'Primorsky', '5a', '59th Motor Rifle Brigade', '59-я мотострелковая бригада'),
    ('60b', 'brigade', 'motor_rifle_brigade', 'Primorsky', '5a', '60th Motor Rifle Brigade', '60-я мотострелковая бригада'),
    ('70b', 'brigade', 'motor_rifle_brigade', 'Primorsky', '5a', '70th Guards Motor Rifle Brigade', '70-я гв. мотострелковая бригада'),
    ('35a', 'army', 'army_troops', 'Amur', 'far', '35th Army', '35-я общевойсковая армия'),
    ('38b', 'brigade', 'motor_rifle_brigade', 'Amur', '35a', '38th Guards Motor Rifle Brigade', '38-я гв. мотострелковая бригада'),
    ('64b', 'brigade', 'motor_rifle_brigade', 'Khabarovsk', '35a', '64th Motor Rifle Brigade', '64-я мотострелковая бригада'),
    ('69b', 'brigade', 'light_brigade', 'Jewish', '35a', '69th Covering Brigade', '69-я бригада прикрытия'),
    ('68c', 'corps', 'army_troops', 'Sakhalin', 'far', '68th Army Corps', '68-й армейский корпус'),
    ('39b', 'brigade', 'motor_rifle_brigade', 'Sakhalin', '68c', '39th Motor Rifle Brigade', '39-я мотострелковая бригада'),
]
USA = [
    ('forscom', 'theatre', 'command', 'Georgia', '', 'Forces Command', 'Командование сил Армии США'),
    ('1c', 'corps', 'corps_troops', 'Washington', 'forscom', 'I Corps', 'I корпус'),
    ('3c', 'corps', 'corps_troops', 'Texas', 'forscom', 'III Corps', 'III корпус'),
    ('1cd', 'division', 'heavy_division', 'Texas', '3c', '1st Cavalry Division', '1-я кавалерийская дивизия'),
    ('4id', 'division', 'heavy_division', 'Colorado', '3c', '4th Infantry Division', '4-я пехотная дивизия'),
    ('1id', 'division', 'heavy_division', 'Kansas', '3c', '1st Infantry Division', '1-я пехотная дивизия'),
    ('18c', 'corps', 'corps_troops', 'North Carolina', 'forscom', 'XVIII Airborne Corps', 'XVIII воздушно-десантный корпус'),
    ('82d', 'division', 'light_division', 'North Carolina', '18c', '82nd Airborne Division', '82-я воздушно-десантная дивизия'),
    ('101d', 'division', 'light_division', 'Kentucky', '18c', '101st Airborne Division', '101-я воздушно-штурмовая дивизия'),
    ('10d', 'division', 'light_division', 'New York', '18c', '10th Mountain Division', '10-я горная дивизия'),
    ('3id', 'division', 'heavy_division', 'Georgia', '18c', '3rd Infantry Division', '3-я пехотная дивизия'),
]
def _china():
    regions = [('shenyang', 'Liaoning', 'Shenyang', 'Шэньянский', [(16, 'Jilin'), (39, 'Liaoning'), (40, 'Liaoning')]),
        ('beijing', 'Beijing', 'Beijing', 'Пекинский', [(27, 'Hebei'), (38, 'Hebei'), (65, 'Hebei')]),
        ('lanzhou', 'Gansu', 'Lanzhou', 'Ланьчжоуский', [(21, 'Shaanxi'), (47, 'Shaanxi')]),
        ('jinan', 'Shandong', 'Jinan', 'Цзинаньский', [(20, 'Henan'), (26, 'Shandong'), (54, 'Henan')]),
        ('nanjing', 'Jiangsu', 'Nanjing', 'Нанкинский', [(1, 'Zhejiang'), (12, 'Jiangsu'), (31, 'Fujian')]),
        ('guangzhou', 'Guangdong', 'Guangzhou', 'Гуанчжоуский', [(41, 'Guangxi'), (42, 'Guangdong')]),
        ('chengdu', 'Sichuan', 'Chengdu', 'Чэндуский', [(13, 'Chongqing'), (14, 'Yunnan')])]
    out = []
    for key, seat, en, ru, armies in regions:
        out.append((key, 'theatre', 'command', seat, '', en + ' Military Region', ru + ' военный округ'))
        for number, where in armies:
            out.append(('%dga' % number, 'army', 'group_army', where, key, '%s Group Army' % _ordinal(number), '%d-я армейская группа' % number))
    return out
def _ordinal(n): return '%d%s' % (n, 'th' if 10 <= n % 100 <= 20 else {1: 'st', 2: 'nd', 3: 'rd'}.get(n % 10, 'th'))
# Regional commands of the Indian Army: seat, people, tanks.
IND = [('north', 'Jammu and Kashmir', 'Northern Command', 'Северное командование', 300000, 300),
    ('west', 'Haryana', 'Western Command', 'Западное командование', 220000, 1400),
    ('southwest', 'Rajasthan', 'South Western Command', 'Юго-Западное командование', 120000, 900),
    ('central', 'Uttar Pradesh', 'Central Command', 'Центральное командование', 80000, 200),
    ('east', 'West Bengal', 'Eastern Command', 'Восточное командование', 250000, 300),
    ('south', 'Maharashtra', 'Southern Command', 'Южное командование', 150000, 900)]
# Armies whose structure is not authored: active army strength and tanks.
SINGLE = {'UKR': (70000, 700), 'DEU': (100000, 400), 'FRA': (130000, 250), 'GBR': (100000, 390), 'ITA': (108000, 320), 'ESP': (79000, 500),
    'POL': (47000, 900), 'TUR': (400000, 4500), 'GRC': (93000, 1600), 'EGY': (340000, 3700), 'ISR': (133000, 3500), 'IRN': (350000, 1600),
    'IRQ': (190000, 150), 'SAU': (75000, 900), 'PAK': (550000, 2400), 'PRK': (950000, 3500), 'KOR': (520000, 2400), 'JPN': (150000, 850),
    'VNM': (412000, 1300), 'THA': (190000, 300), 'IDN': (233000, 0), 'BRA': (190000, 270), 'MEX': (200000, 0), 'COL': (235000, 0),
    'DZA': (110000, 1000), 'SYR': (220000, 4900), 'BLR': (30000, 500), 'KAZ': (30000, 980), 'MMR': (375000, 150), 'ETH': (135000, 250),
    'MAR': (175000, 380), 'BGD': (126000, 230), 'TWN': (200000, 900)}

def build(P, capital, prov_pop, people_per_person, country_name):
    """Rows for the land-force tables and the names of the formations.

    P: provinces (id, name, owner); capital: owner -> province id."""
    rows = {k: [] for k in ('military_equipment', 'formation_templates', 'formation_template_equipment', 'formation_template_consumables',
        'military_formations', 'formation_equipment', 'formation_consumables', 'military_stockpiles', 'formation_labels')}
    english, russian, missing = [], [], []
    # How many people one pop unit stands for; the scenario's English text has it, the Russian needs it too.
    for key in ('katechon_people_per_unit', 'katechon_people_per_pop_unit'): russian.append((key, '%d' % int(4 * people_per_person)))
    for category, (en, ru) in sorted(EQUIPMENT_NAMES.items()):
        english.append(('katechon_mil_eq_%d' % category, en)); russian.append(('katechon_mil_eq_%d' % category, ru))
    owners = {p['owner'] for p in P}
    def number(x): return ('%.6f' % x).rstrip('0').rstrip('.')
    def persons(people): return max(1, int(round(people / people_per_person)))
    def plain(text): return ''.join(c for c in unicodedata.normalize('NFKD', text).lower() if 'a' <= c <= 'z')
    def province(owner, name):
        found = [p for p in P if p['owner'] == owner and plain(name) in plain(p['name'])]
        if not found: missing.append('%s: %s' % (owner, name)); return None
        return max(found, key=lambda p: prov_pop.get(p['id'], 0))['id']  # of namesakes, the populous one
    for e in EQUIPMENT: rows['military_equipment'].append('%s;%d;%s;%s;%s;%s;%s;%s' % e)
    templates = {}
    def template(key, people, equipment):
        if key in templates: return
        templates[key] = (persons(people), equipment)
        rows['formation_templates'].append('%s;%d' % (key, persons(people)))
        for model, pieces in equipment.items():
            if pieces: rows['formation_template_equipment'].append('%s;%s;%d' % (key, model, pieces))
        for kind, commodity, per_person, per_tonne in CONSUMABLES:
            rows['formation_template_consumables'].append('%s;%s;%s;%s;%s' % (key, kind, commodity, number(per_person), number(per_tonne)))
    need = {}  # owner -> depot province -> consumable -> a day at full tempo
    def formation(owner, key, echelon, template_key, where, parent, en, ru, depot):
        fid = '%s_%s' % (owner, key)
        strength, equipment = templates[template_key]
        rows['military_formations'].append('%s;%s;%s;%s;%d;active;0.2;' % (fid, ('%s_%s' % (owner, parent)) if parent else '', template_key, owner, where))
        rows['formation_labels'].append('%s;%s;kf_%s;%d' % (fid, echelon, fid, strength))
        english.append(('kf_' + fid, en)); russian.append(('kf_' + fid, ru))
        tonnes = sum(MASS[m] * n for m, n in equipment.items()) / 1000.0
        for model, pieces in equipment.items():
            if pieces: rows['formation_equipment'].append('%s;%s;%d' % (fid, model, pieces))
        for kind, commodity, per_person, per_tonne in CONSUMABLES:
            day = per_person * strength + per_tonne * tonnes
            # a month in hand at the pace of peacetime (tempo 0.2 draws 0.4 of the full rate)
            rows['formation_consumables'].append('%s;%s;%s' % (fid, kind, number(day * 0.4 * 30)))
            need.setdefault(owner, {}).setdefault(depot, {}).setdefault(kind, 0.0)
            need[owner][depot][kind] += day
    def structured(owner, table):
        if owner not in owners: return
        seat = {}
        for key, echelon, template_key, where, parent, en, ru in table:
            at = province(owner, where)
            if at is None: continue
            people, equipment = TEMPLATES[template_key]
            template(template_key, people, equipment)
            # a formation draws on the depot at the seat of its district or command
            top = key
            parents = {k: p for k, _, _, _, p, _, _ in table}
            while parents.get(top): top = parents[top]
            if not parent: seat[key] = at
            formation(owner, key, echelon, template_key, at, parent, en, ru, seat.get(top, at))
    structured('RUS', RUS)
    structured('USA', USA)
    structured('CHN', _china())
    if 'IND' in owners:
        for key, where, en, ru, people, tanks in IND:
            at = province('IND', where)
            if at is None: continue
            template('IND_' + key, people, {'tank': tanks, 'small_arms': people})
            formation('IND', key, 'theatre', 'IND_' + key, at, '', en, ru, at)
    for owner, (people, tanks) in sorted(SINGLE.items()):
        if owner not in owners or owner not in capital: continue
        template('army_' + owner, people, {'tank': tanks, 'small_arms': people})
        formation(owner, 'army', '', 'army_' + owner, capital[owner], '', 'Army', 'Сухопутные войска', capital[owner])
    # Depots hold a year of what the formations they serve would use at full tempo.
    for owner, depots in sorted(need.items()):
        for where, kinds in sorted(depots.items()):
            for kind, day in sorted(kinds.items()):
                rows['military_stockpiles'].append('%s_depot_%d_%s;%s;%d;depot;consumable;;%s;%s' % (owner, where, kind, owner, where, kind, number(day * 365)))
    return rows, english, russian, missing
