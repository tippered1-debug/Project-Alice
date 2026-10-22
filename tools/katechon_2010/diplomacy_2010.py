"""Relations between states on 1 January 2010, for the KATECHON 2010 scenario.

The game keeps one relation value per pair of states, from -200 (hostile) to
+200 (cordial). Here it is made of two parts:

* membership of the same bloc or union, which brings states closer the more of
  them they share (NATO, the EU, the CSTO, the Union State, the Gulf
  Cooperation Council and the like);
* the bilateral standing that day, where it is known to be good or bad: open
  conflicts, frozen ones, disputes, close partnerships.

The figures are judgements of the kind a careful reader of the 2009 press would
make, rounded to tens. A pair not named stays at 0, which the game calls correct.

The scenario writes them as one hidden decision per country (the game applies
the effects of a decision named in a country's history when the scenario is
built); each pair is written once, in the decision of the first tag.
"""

BLOCS = {
	# name: (pair value, members)
	'nato': (60, 'USA CAN GBR FRA DEU ITA ESP PRT NLD BEL LUX DNK NOR ISL GRC TUR POL CZE HUN SVK SVN EST LVA LTU ROU BGR HRV ALB'),
	'eu': (70, 'AUT BEL BGR CYP CZE DEU DNK ESP EST FIN FRA GBR GRC HUN IRL ITA LTU LUX LVA MLT NLD POL PRT ROU SVK SVN SWE'),
	'nordic': (60, 'DNK FIN ISL NOR SWE'),
	'csto': (70, 'RUS BLR KAZ ARM KGZ TJK UZB'),
	'cis': (20, 'RUS BLR UKR MDA ARM AZE KAZ KGZ TJK UZB'),
	'sco': (30, 'CHN RUS KAZ KGZ TJK UZB'),
	'gcc': (90, 'SAU ARE KWT QAT BHR OMN'),
	'arab_league': (30, 'SAU ARE KWT QAT BHR OMN YEM IRQ JOR SYR LBN PSX EGY LBY TUN DZA MAR MRT SDN SOM DJI COM'),
	'asean': (40, 'IDN MYS PHL SGP THA VNM LAO KHM MMR BRN'),
	'mercosur': (60, 'BRA ARG URY PRY'),
	'alba': (80, 'VEN CUB BOL NIC ECU DMA VCT ATG'),
	'anzus': (100, 'AUS NZL'),
	'sacu': (50, 'ZAF BWA NAM LSO SWZ'),
	'eac': (40, 'KEN TZA UGA RWA BDI'),
	'ecowas': (20, 'NGA GHA SEN CIV MLI BFA NER GIN SLE LBR TGO BEN GMB GNB CPV'),
	'commonwealth_realms': (50, 'GBR CAN AUS NZL'),
}

# (a, b, value): the bilateral standing on 1 January 2010, added to what blocs give.
PAIRS = [
	# Russia
	('RUS', 'BLR', 120), ('RUS', 'KAZ', 90), ('RUS', 'ARM', 80), ('RUS', 'KGZ', 50), ('RUS', 'TJK', 40), ('RUS', 'UZB', 20),
	('RUS', 'GEO', -170), ('RUS', 'UKR', -10), ('RUS', 'MDA', -30), ('RUS', 'AZE', 30),
	('RUS', 'USA', -40), ('RUS', 'GBR', -70), ('RUS', 'POL', -50), ('RUS', 'EST', -90), ('RUS', 'LVA', -70), ('RUS', 'LTU', -70),
	('RUS', 'DEU', 40), ('RUS', 'FRA', 30), ('RUS', 'ITA', 50), ('RUS', 'TUR', 30), ('RUS', 'FIN', 20), ('RUS', 'JPN', -30),
	('RUS', 'CHN', 80), ('RUS', 'IND', 90), ('RUS', 'SRB', 100), ('RUS', 'VEN', 80), ('RUS', 'CUB', 70), ('RUS', 'SYR', 70),
	('RUS', 'IRN', 20), ('RUS', 'VNM', 50), ('RUS', 'MNG', 40), ('RUS', 'KOS', -120), ('RUS', 'CZE', -20),
	# United States and its partners
	('USA', 'GBR', 160), ('USA', 'CAN', 140), ('USA', 'AUS', 150), ('USA', 'NZL', 80), ('USA', 'JPN', 150), ('USA', 'KOR', 150),
	('USA', 'ISR', 170), ('USA', 'TWN', 110), ('USA', 'PHL', 90), ('USA', 'THA', 60), ('USA', 'SGP', 80), ('USA', 'MEX', 60),
	('USA', 'COL', 110), ('USA', 'SAU', 80), ('USA', 'EGY', 60), ('USA', 'JOR', 70), ('USA', 'ARE', 60), ('USA', 'KWT', 80),
	('USA', 'QAT', 60), ('USA', 'BHR', 70), ('USA', 'IRQ', 40), ('USA', 'AFG', 40), ('USA', 'PAK', 10), ('USA', 'IND', 60),
	('USA', 'GEO', 100), ('USA', 'UKR', 40), ('USA', 'POL', 60), ('USA', 'KOS', 120), ('USA', 'DEU', 40), ('USA', 'FRA', 30),
	('USA', 'CHN', -30), ('USA', 'CUB', -150), ('USA', 'VEN', -130), ('USA', 'IRN', -190), ('USA', 'PRK', -190), ('USA', 'SYR', -110),
	('USA', 'SDN', -120), ('USA', 'MMR', -100), ('USA', 'ZWE', -90), ('USA', 'BLR', -80), ('USA', 'BOL', -70), ('USA', 'NIC', -40),
	('USA', 'LBY', -30), ('USA', 'ERI', -60),
	('GBR', 'IRL', 60), ('GBR', 'ARG', -80), ('GBR', 'FRA', 60), ('GBR', 'IRN', -110),
	('CAN', 'AUS', 60), ('AUS', 'NZL', 90), ('AUS', 'IDN', 30), ('JPN', 'AUS', 60),
	# Europe
	('DEU', 'FRA', 120), ('DEU', 'POL', 40), ('DEU', 'NLD', 50), ('DEU', 'AUT', 80), ('FRA', 'BEL', 60), ('ITA', 'FRA', 40),
	('ESP', 'PRT', 70), ('GRC', 'CYP', 150), ('GRC', 'TUR', -50), ('GRC', 'MKD', -70), ('CYP', 'TUR', -150),
	('SRB', 'KOS', -180), ('SRB', 'HRV', -40), ('SRB', 'BIH', -20), ('ALB', 'KOS', 150), ('ROU', 'MDA', 60), ('HUN', 'SVK', -40),
	('POL', 'UKR', 70), ('POL', 'LTU', 40), ('POL', 'BLR', -60), ('EST', 'FIN', 90), ('SWE', 'FIN', 60), ('CHE', 'DEU', 40),
	('BLR', 'LTU', -30), ('UKR', 'GEO', 60), ('UKR', 'MDA', 20),
	# Caucasus and Central Asia
	('ARM', 'AZE', -190), ('ARM', 'TUR', -120), ('AZE', 'TUR', 160), ('GEO', 'AZE', 60), ('GEO', 'TUR', 50), ('ARM', 'IRN', 50),
	('KAZ', 'UZB', 10), ('UZB', 'KGZ', -40), ('UZB', 'TJK', -60), ('TKM', 'UZB', -20), ('KAZ', 'CHN', 50),
	# Middle East
	('ISR', 'IRN', -200), ('ISR', 'SYR', -180), ('ISR', 'LBN', -170), ('ISR', 'PSX', -150), ('ISR', 'EGY', 10), ('ISR', 'JOR', 10),
	('ISR', 'TUR', -40), ('ISR', 'SAU', -80), ('ISR', 'IRQ', -90), ('ISR', 'LBY', -120),
	('IRN', 'SAU', -90), ('IRN', 'ARE', -40), ('IRN', 'BHR', -60), ('IRN', 'IRQ', 60), ('IRN', 'SYR', 140), ('IRN', 'VEN', 80),
	('IRN', 'LBN', 40), ('IRN', 'AFG', 0), ('IRN', 'TUR', 30), ('SYR', 'LBN', 10), ('SYR', 'TUR', 70), ('SYR', 'IRQ', -30),
	('SAU', 'YEM', -10), ('SAU', 'EGY', 60), ('SAU', 'PAK', 90), ('EGY', 'SDN', 20), ('IRQ', 'KWT', -30),
	# South Asia
	('IND', 'PAK', -160), ('IND', 'AFG', 60), ('IND', 'BGD', 20), ('IND', 'NPL', 40), ('IND', 'BTN', 120), ('IND', 'LKA', 40),
	('IND', 'JPN', 50), ('IND', 'ISR', 60), ('PAK', 'AFG', -60), ('PAK', 'TUR', 60),
	# East and South-East Asia
	('CHN', 'PRK', 120), ('CHN', 'PAK', 160), ('CHN', 'TWN', -150), ('CHN', 'JPN', -50), ('CHN', 'IND', -40), ('CHN', 'VNM', -30),
	('CHN', 'MMR', 70), ('CHN', 'KHM', 70), ('CHN', 'LAO', 70), ('CHN', 'IRN', 40), ('CHN', 'ZWE', 50), ('CHN', 'SDN', 70),
	('CHN', 'VEN', 40), ('CHN', 'KOR', 20), ('CHN', 'MNG', 10), ('CHN', 'PHL', -20), ('CHN', 'LKA', 50),
	('KOR', 'PRK', -180), ('JPN', 'PRK', -170), ('JPN', 'KOR', 30), ('JPN', 'TWN', 50), ('THA', 'KHM', -90), ('VNM', 'KHM', 40),
	('VNM', 'LAO', 120), ('MYS', 'SGP', 30), ('IDN', 'TLS', 30),
	# Africa
	('ETH', 'ERI', -170), ('ERI', 'DJI', -90), ('ETH', 'SOM', -80), ('SDN', 'TCD', -80), ('MAR', 'DZA', -90), ('LBY', 'TCD', 0),
	('RWA', 'COD', -60), ('UGA', 'COD', -40), ('ZAF', 'ZWE', 20), ('NGA', 'GHA', 40), ('KEN', 'ETH', 40), ('EGY', 'ETH', -30),
	('LBY', 'SDN', 20), ('CIV', 'BFA', 30),
	# The Americas
	('VEN', 'COL', -120), ('VEN', 'CUB', 170), ('VEN', 'BOL', 120), ('VEN', 'NIC', 100), ('VEN', 'ECU', 80), ('ECU', 'COL', -60),
	('PER', 'CHL', -20), ('BOL', 'CHL', -60), ('ARG', 'URY', -20), ('BRA', 'ARG', 50), ('CUB', 'BOL', 80), ('MEX', 'CUB', 10),
	('CAN', 'MEX', 40), ('BRA', 'VEN', 30), ('PRY', 'BOL', 0),
]

STARTING_DIPLOMATS = 4.0
MONTHLY_DIPLOMATS = 1.0  # the base game gives 0.3: diplomacy of 2010 runs faster than that of 1836


def relations(owners):
	"""Every pair of live states with a relation other than 0, as {(a, b): value} with a < b."""
	live = set(owners)
	values = {}

	def add(a, b, v):
		if a == b or a not in live or b not in live:
			return
		key = (a, b) if a < b else (b, a)
		values[key] = values.get(key, 0) + v

	for value, members in BLOCS.values():
		tags = [t for t in members.split() if t in live]
		for i, a in enumerate(tags):
			for b in tags[i + 1:]:
				add(a, b, value)
	seen = set()
	for a, b, v in PAIRS:
		key = (a, b) if a < b else (b, a)
		assert key not in seen, 'pair named twice: %s %s' % key
		seen.add(key)
		add(a, b, v)
	return {k: max(-200, min(200, int(round(v / 10.0) * 10))) for k, v in values.items() if v}


def decision_name(tag):
	return 'katechon_start_%s' % tag


def write(w, loc_add, owners):
	"""Writes decisions/katechon_2010_start.txt and returns the history line for each tag."""
	pairs = relations(owners)
	by_first = {}
	for (a, b), v in sorted(pairs.items()):
		by_first.setdefault(a, []).append((b, v))
	text = 'political_decisions = {\n'
	for tag in owners:
		text += '\t%s = {\n\t\tpotential = { always = no }\n\t\tallow = { always = no }\n\t\teffect = {\n' % decision_name(tag)
		text += '\t\t\tdiplo_points = %.1f\n' % STARTING_DIPLOMATS
		for other, v in by_first.get(tag, []):
			text += '\t\t\trelation = { who = %s value = %d }\n' % (other, v)
		text += '\t\t}\n\t\tai_will_do = { factor = 0 }\n\t}\n'
		loc_add(decision_name(tag) + '_title', 'The world on 1 January 2010')
		loc_add(decision_name(tag) + '_desc', 'Relations between states as they stood on the first day of 2010.')
	text += '}\n'
	w('decisions/katechon_2010_start.txt', text)
	return {tag: 'decision = %s\n' % decision_name(tag) for tag in owners}, len(pairs)
