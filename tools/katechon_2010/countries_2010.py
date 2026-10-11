"""Hand-entered 2010 facts per state: majority culture and religion in the game's
vocabulary, and the form of government. D democracy, M constitutional monarchy,
A absolute monarchy, P presidential or military dictatorship, C one-party communist state."""
T = """
AFG pashtun sunni D|AGO ovimbundu catholic P|ALB albanian sunni D|ADR catalan catholic D|ARE bedouin sunni A|ARG platinean catholic D
ARM armenian orthodox D|ATG afro_caribbean protestant M|AUS australian protestant M|AUT south_german catholic D|AZE azerbaijani shiite P
BDI rundi catholic D|BEL flemish catholic M|BEN fon catholic D|BFA mossi sunni P|BGD bengali sunni D|BGR bulgarian orthodox D
BHR bedouin shiite A|BHS afro_caribbean protestant M|BIH bosniak sunni D|BLR byelorussian orthodox P|BLZ central_american catholic M
BOL aimara catholic D|BRA brazilian catholic D|BRB afro_caribbean protestant M|BRN malay sunni A|BTN tibetan gelugpa M|BWA tswana protestant D
CAF sara protestant P|CAN anglo_canadian catholic M|CHE swiss catholic D|CHL south_andean catholic D|CHN beifaren mahayana C
CIV akan sunni P|CMR fang catholic P|COD luba catholic P|COG bakongo catholic P|COL north_andean catholic D|COM swahili sunni D
CPV afro_caribbean catholic D|CRI central_american catholic D|CUB caribeno catholic C|CYP greek orthodox D|CZE czech catholic D
DEU north_german protestant D|DJI somali sunni P|DMA afro_caribbean catholic D|DNK danish protestant M|DOM caribeno catholic D
DZA maghrebi sunni P|ECU north_andean catholic D|EGY misri sunni P|ERI tigray orthodox P|ESP spanish catholic M|EST estonian protestant D
ETH amhara orthodox P|FIN finnish protestant D|FJI melanesian protestant P|FRA french catholic D|FSM micronesian catholic D
GAB fang catholic P|GBR british protestant M|GEO georgian orthodox D|GHA akan protestant D|GIN fulbe sunni P|GMB mande sunni P
GNB fulbe sunni D|GNQ fang catholic P|GRC greek orthodox D|GRD afro_caribbean catholic M|GTM mayan catholic D|GUY afro_caribbean protestant D
HND central_american catholic D|HRV croat catholic D|HTI afro_antillean catholic D|HUN hungarian catholic D|IDN javan sunni D
IND avadhi hindu D|IRL irish catholic D|IRN persian shiite P|IRQ mashriqi shiite D|ISL icelandic protestant D|ISR ashkenazi jewish D
ITA north_italian catholic D|JAM afro_caribbean protestant M|JOR mashriqi sunni M|JPN japanese shinto M|KAZ kazak sunni P
KEN kikuyu protestant D|KGZ kirgiz sunni D|KHM khmer theravada M|KIR micronesian catholic D|KNA afro_caribbean protestant M
KOR korean mahayana D|KOS albanian sunni D|KWT bedouin sunni M|LAO lao theravada C|LBN mashriqi sunni D|LBR kru protestant D
LBY maghrebi sunni P|LCA afro_caribbean catholic M|LIE south_german catholic M|LKA sinhala theravada D|LSO sotho protestant M
LTU lithuanian catholic D|LUX south_german catholic M|LVA latvian protestant D|MAR maghrebi sunni M|MCO french catholic M
MDA romanian orthodox D|MDG malagasy protestant P|MDV sinhala sunni D|MEX mexican catholic D|MHL micronesian protestant D
MKD bulgarian orthodox D|MLI bambara sunni D|MLT maltese catholic D|MMR burmese theravada P|MNE serb orthodox D|MNG mongol gelugpa D
MOZ makua catholic D|MRT maures sunni P|MUS afro_caribbean hindu D|MWI chewa protestant D|MYS malay sunni M|NAM herero protestant D
NER hausa sunni P|NGA hausa sunni D|NIC central_american catholic D|NLD dutch protestant M|NOR norwegian protestant M
NPL nepali hindu D|NRU micronesian protestant D|NZL australian protestant M|OMN bedouin sunni A|PAK panjabi sunni D
PAN central_american catholic D|PER quechua catholic D|PHL filipino catholic D|PLW micronesian catholic D|PNG melanesian protestant M
POL polish catholic D|PRK korean mahayana C|PRT portuguese catholic D|PRY guarani catholic D|PSX mashriqi sunni D|QAT bedouin sunni A
ROU romanian orthodox D|RUS russian orthodox D|RWA ruanda catholic P|SAU bedouin sunni A|SDN sudanese sunni P|SEN wolof sunni D
SGP min mahayana D|SLB melanesian protestant M|SLE mande sunni D|SLV central_american catholic D|SMR north_italian catholic D
SOM somali sunni P|SRB serb orthodox D|STP afro_caribbean catholic D|SUR afro_caribbean protestant D|SVK slovak catholic D
SVN slovene catholic D|SWE swedish protestant M|SWZ nguni protestant A|SYC afro_caribbean catholic D|SYR mashriqi sunni P
TCD sara sunni P|TGO ewe catholic P|THA thai theravada M|TJK tajik sunni P|TKM turkmen sunni P|TLS malay catholic D
TON polynesian protestant M|TTO afro_caribbean catholic D|TUN maghrebi sunni P|TUR turkish sunni D|TUV polynesian protestant M
TWN min mahayana D|TZA sukuma protestant D|UGA baganda catholic P|UKR ukrainian orthodox D|URY platinean catholic D
USA yankee protestant D|UZB uzbek sunni P|VAT north_italian catholic A|VCT afro_caribbean protestant M|VEN north_andean catholic D
VNM vietnamese mahayana C|VUT melanesian protestant D|WSM polynesian protestant D|YEM bedouin sunni P|ZAF zulu protestant D
ZMB tonga protestant D|ZWE shona protestant P
"""
COUNTRY = {}
for chunk in T.replace("\n", "|").split("|"):
    parts = chunk.split()
    if len(parts) == 4:
        COUNTRY[parts[0]] = (parts[1], parts[2], parts[3])
GOVERNMENT = {"D": "democracy", "M": "hms_government", "A": "absolute_monarchy", "P": "presidential_dictatorship", "C": "proletarian_dictatorship"}
