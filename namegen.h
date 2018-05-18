/* Copyright (c) 2002 Rick Dearman <rick@ricken.demon.co.uk>
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
* LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
* OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
* WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef NAMEGEN_H
#define NAMEGEN_H

char *nameCollection[] = { "Uncharted Space",
  /* Consellation Names */
  "Andromeda", "Antlia", "Apus", "Aquarius", "Aquila", "Ara", "Aries",
  "Auriga", "Bootes", "Caelum", "Camelopardalis", "Cancer", "Canes",
  "Canis", "Canis", "Capricornus", "Carina", "Cassiopeia", "Centaurus",
  "Cepheus", "Cetus", "Chamaleon", "Circinus", "Columba", "Coma", "Corona",
  "Corona", "Corvus", "Crater", "Crux", "Cygnus", "Delphinus", "Dorado",
  "Draco", "Equuleus", "Eridanus", "Fornax", "Gemini", "Grus", "Hercules",
  "Horologium", "Hydra", "Hydrus", "Indus", "Lacerta", "Leo", "Leo",
  "Lepus", "Libra", "Lupus", "Lynx", "Lyra", "Mensa", "Microscopium",
  "Monoceros", "Musca", "Norma", "Octans", "Ophiucus", "Orion", "Pavo",
  "Pegasus", "Perseus", "Phoenix", "Pictor", "Pisces", "Pisces", "Puppis",
  "Pyxis", "Reticulum", "Sagitta", "Sagittarius", "Scorpius", "Sculptor",
  "Scutum", "Serpens", "Sextans", "Taurus", "Telescopium", "Triangulum",
  "Triangulum", "Tucana", "Ursa", "Ursa", "Vela", "Virgo", "Volans",
  "Vulpecula",
  /* Greek Star Names */
  "Theta Eri", "Alpha Eri", "Beta Sco", "Alpha Cnc", "Zeta Leo",
  "Epsilon CMa", "Epsilon Tau", "Epsilon Aqr", "Alpha Crv", "Alpha Tau",
  "Alpha Cep", "Beta Cep", "Alpha Cap", "Gamma Peg", "Gamma Leo",
  "Beta Ori", "Beta Per", "Delta Crv", "Gamma Gem", "Epsilon UMa",
  "Eta UMa", "Alpha Crt", "Gamma And", "Gamma Gem", "Alpha Gru",
  "Zeta Cen", "Epsilon Ori", "Zeta Ori", "Alpha Hya", "Alpha CrB",
  "Alpha And", "Sigma Dra", "Lambda Vel", "Alpha Aql", "Delta Dra",
  "Lambda Leo", "Eta CMa", "Xi UMa", "Nu UMa", "Theta Ser", "Tau2 Eri",
  "Alpha Phe", "Beta Sgr", "Alpha Lep", "Mu Dra", "Omicron Per",
  "Delta Vir", "Eta Eri", "Theta Peg", "Zeta Cet", "Omicron1 Eri",
  "Eta UMa", "Alpha Ori", "Delta Ari", "Beta Cas", "Beta Oph", "Theta Leo",
  "Beta Eri", "Beta Cap", "Alpha Cyg", "Epsilon Del", "Delta Cap",
  "Beta Cet", "Beta Leo", "Beta Cet", "Delta Sco", "Alpha UMa", "Psi Dra",
  "Iota Dra", "Beta Tau", "Gamma Dra", "Epsilon Peg", "Gamma Cep",
  "Alpha PsA", "Zeta CMa", "Gamma Crv", "Epsilon Cyg", "Beta CMi",
  "Beta Cen", "Alpha Ari", "Lambda Ori", "Zeta Peg", "Epsilon Boo",
  "Nu Sco", "Gamma Cet", "Epsilon Sgr", "Lambda Sgr", "Delta Sgr",
  "Omicron2 Eri", "Alpha Equ", "Beta UMi", "Xi Cep", "Upsilon Sco",
  "Lambda Her", "Epsilon Aur", "Lambda Oph", "Alpha Peg", "Eta Peg",
  "Epsilon Gem", "Delta UMa", "Lambda Ori", "Zeta Gem", "Beta Aur",
  "Alpha Cet", "Xi Per", "Beta UMa", "Delta Ori", "Beta And", "Alpha Per",
  "Zeta UMa", "Alpha Tri", "Eta Boo", "Beta CMa", "Gamma Cap", "Beta Boo",
  "Gamma Sgr", "Beta Lep", "Beta CrB", "Alpha Psc", "Alpha Col",
  "Gamma UMa", "Gamma UMi", "Mu Leo", "Alpha Her", "Alpha Oph", "Beta Dra",
  "Beta Ori", "Alpha Cen", "Alpha Psc", "Delta Cas", "Alpha Sgr", "Eta Oph",
  "Gamma Aqr", "Mu Peg", "Alpha Aqr", "Beta Aqr", "Gamma Cyg", "Kappa Ori",
  "Beta Peg", "Lambda Sco", "Alpha Cas", "Beta Ari", "Alpha And",
  "Delta Aqr", "Gamma Lyr", "Mu UMa", "Lambda UMa", "Kappa UMa",
  "Iota UMa", "Beta Cnc", "Alpha Dra", "Alpha Ser", "Alpha Lyr",
  "Delta Gem", "Beta Col", "Delta CMa", "Delta Oph", "Epsilon Oph",
  "Epsilon Vir", "Gamma Eri", "Beta Vir", "Alpha Lib", "Beta Lib",
  /* Arabic Star Names */
  "Acamar", "Achernar", "Acrab", "Acubens", "Adhafera", "Adhara", "Ain",
  "Albali", "Alchibah", "Aldebaran", "Alderamin", "Alfirk", "Algedi",
  "Algenib", "Algieba", "Algebar", "Algol", "Algorab", "Alhena",
  "Alioth", "Alkaid", "Alkes", "Almak", "Almeisan", "Alnair", "Alnair",
  "Alnilam", "Alnitak", "Alphard", "Alphecca", "Alpheratz", "Alsafi",
  "Alsuhail", "Altair", "Altais", "Alterf", "Aludra", "Alula Australis",
  "Alula Borealis", "Alya", "Angetenar", "Ankaa", "Arkab", "Arneb",
  "Arrakis", "Atik", "Auva", "Azha", "Baham", "Baten Kaitos", "Beid",
  "Benetnash", "Betelgeuse", "Botein", "Caph", "Celbalrai", "Chort",
  "Cursa", "Dabih", "Deneb", "Deneb", "Deneb Algedi", "Deneb Kaitos",
  "Denebola", "Diphda", "Dschubba", "Dubhe", "Dziban", "Edasich",
  "El Nath", "Eltanin", "Enif", "Errai", "Fomalhaut", "Furud", "Gienah",
  "Gienah", "Gomeisa", "Hadar", "Hamal", "Heka", "Homam", "Izar",
  "Jabbah", "Kaffaljidhma", "Kaus Australis", "Kaus Borealis",
  "Kaus Media", "Keid", "Kitalpha", "Kokab", "Kurhah", "Lesath",
  "Maasym", "Maaz", "Marfik", "Markab", "Matar", "Mebsuta", "Megrez",
  "Meissa", "Mekbuda", "Menkalinan", "Menkar", "Menkib", "Merak",
  "Mintaka", "Mirak", "Mirfak", "Mizar", "Mothallah", "Muphrid", "Murzim",
  "Nashira", "Nekkar", "Nasl", "Nihal", "Nusakan", "Okda", "Phact",
  "Phad", "Pherkad", "Rasalased", "Rasalgethi", "Rasalhague", "Rastaban",
  "Rigel", "Rigilkent", "Risha", "Rukbah", "Rukbat", "Sabik",
  "Sadachbia", "Sadalbari", "Sadalmelik", "Sadalsuud", "Sadr", "Saiph",
  "Scheat", "Shaula", "Shedir", "Sheratan", "Sirrah", "Skat", "Sulafat",
  "Tania Australis", "Tania Borealis", "Talitha Australis",
  "Talitha Borealis", "Tarf", "Thuban", "Unukalhai", "Vega", "Wasat",
  "Wazn", "Wezen", "Yed Prior", "Yed Posterior", "Zaniah", "Zaurac",
  "Zavijava", "Zubenelgenubi", "Zubeneshamali"
};
// int *usedNames;
int nameCount;


char *firstSyllable[] = {	/* 100 */
  "A", "Ab", "Ac", "Add", "Ad", "Af", "Aggr", "Ax", "Az", "Bat", "Be", "Byt",
  "Cyth", "Agr", "Ast", "As", "Al", "Adw", "Adr", "Ar", "B", "Br", "C",
  "Cr", "Ch", "Cad", "D", "Dr", "Dw", "Ed", "Eth", "Et", "Er", "El", "Eow",
  "F", "Fr", "Ferr", "G", "Gr", "Gw", "Gw", "Gal", "Gl", "H", "Ha", "Ib",
  "Jer", "K", "Ka", "Ked", "L", "Loth", "Lar", "Leg", "M", "Mir", "N",
  "Jer", "K", "Ka", "Ked", "L", "Loth", "Lar", "Leg", "M", "Mir", "N",
  "Nyd", "Ol", "Oc", "On", "P", "Pr", "R", "Rh", "S", "Sev", "T", "Tr",
  "Th", "Th", "V", "Y", "Yb", "Z", "W", "Wic", "Wac", "Wer", "Fert",
  "D'al", "Fl'a", "L'Dre", "Ra", "Rea", "Og", "O'g", "Ndea", "Faw", "Cef",
  "Cyth", "Wyh", "Gyh", "G'As", "Red", "Aas", "Aaw", "Ewwa", "Syw"
};
char *middleSyllable[] = {	/* 50 */
  "a", "ase", "ae", "ae", "au", "ao", "are", "ale", "ali", "ay", "ardo", "e",
  "ere", "ehe", "eje", "eo", "ei", "ea", "ea", "eye", "eri", "era", "ela",
  "eli", "enda", "erra", "i", "ia", "ioe", "itti", "otte", "ie", "ire",
  "eli", "enda", "erra", "i", "ia", "ioe", "itti", "otte", "ie", "ire",
  "ira", "ila", "ili", "illi", "ira", "igo", "o", "oje", "oli", "olye",
  "ua", "ue", "uyye", "oa", "oi", "oe", "ore"
};
char *lastSyllable[] = {	/* 100 */
  "and", "be", "bwyn", "baen", "bard", "ctred", "cred", "ch", "can", "dan",
  "don", "der", "dric", "dfrid", "dus", "gord", "gan", "li", "le", "lgrin",
  "lin", "lith", "lath", "loth", "ld", "ldric", "ldan", "mas", "mos", "mar",
  "ond", "ydd", "idd", "nnon", "wan", "yth", "nad", "nn", "nor", "nd",
  "ron", "rd", "sh", "seth", "ean", "th", "threm", "tha", "tan", "tem",
  "ron", "rd", "sh", "seth", "ean", "th", "threm", "tha", "tan", "tem",
  "tam", "vix", "vud", "wix", "wan", "win", "wyn", "wyr", "wyth", "zer",
  "zan", "qela", "rli", "wa", "kera", "ji", "jia", "jioe", "jiti", "jote",
  "kie", "hireg", "jira", "fila", "vili", "xilli", "cira", "digo", "no",
  "noje", "woli", "yolye", "tua", "tue", "tye", "toa", "toi", "toe", "tore",
  "apd", "pe", "btyn", "brrin", "berd", "cfed", "cadf", "cac", "cane",
  "fdan", "fdon"
};


#endif
