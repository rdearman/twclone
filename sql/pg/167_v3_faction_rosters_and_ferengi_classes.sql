-- V3 Ferengi ship classes and explicit Ferengi/Orion NPC rosters.
-- Additive and idempotent: existing players, ships, cargo, offers and history
-- are retained. BigBang is not required.
BEGIN;

INSERT INTO shiptypes
    (name, basecost, maxattack, initialholds, maxholds, maxfighters, turns,
     maxmines, maxlimpets, maxgenesis, max_detonators, max_probes,
     can_transwarp, transportrange, maxshields, offense, defense, maxbeacons,
     can_long_range_scan, can_planet_scan, maxphotons, max_cloaks,
     can_purchase, has_escape_pod, enabled)
VALUES
    ('Ferengi Trade Master', 90000, 1000, 60, 250, 3000, 4, 5, 0, 2, 0, 0, FALSE, 5, 2000, 6, 8, 20, TRUE, TRUE, 0, 0, FALSE, FALSE, TRUE),
    ('Ferengi Cruiser', 115000, 3000, 30, 120, 8000, 3, 15, 0, 3, 0, 0, FALSE, 7, 3000, 12, 12, 30, TRUE, TRUE, 0, 0, FALSE, FALSE, TRUE),
    ('Ferengi Interceptor', 75000, 2500, 10, 35, 4000, 1, 5, 0, 0, 0, 0, FALSE, 3, 1500, 18, 7, 5, TRUE, FALSE, 0, 1, FALSE, FALSE, TRUE),
    ('Ferengi Dreadnought', 350000, 12000, 15, 75, 40000, 8, 50, 0, 5, 0, 0, TRUE, 12, 10000, 20, 20, 100, TRUE, TRUE, 2, 0, FALSE, FALSE, TRUE)
ON CONFLICT (name) DO NOTHING;

UPDATE shiptypes SET default_personality = 'defensive'
 WHERE name = 'Orion Heavy Fighter Patrol' AND default_personality IS NULL;
UPDATE shiptypes SET default_personality = 'looter'
 WHERE name = 'Orion Scout/Looter' AND default_personality IS NULL;
UPDATE shiptypes SET default_personality = 'offensive'
 WHERE name = 'Orion Contraband Runner' AND default_personality IS NULL;
UPDATE shiptypes SET default_personality = 'balanced'
 WHERE name = 'Orion Smuggler''s Kiss' AND default_personality IS NULL;
UPDATE shiptypes SET default_personality = 'blockader'
 WHERE name = 'Orion Black Market Guard' AND default_personality IS NULL;

CREATE TEMP TABLE ferengi_v3_names (position integer PRIMARY KEY, display_name text NOT NULL) ON COMMIT DROP;
INSERT INTO ferengi_v3_names(position, display_name) VALUES
 (1,'Gaila the Wayfarer'),(2,'Moogie''s Ledger'),(3,'Vek of the Freehold'),
 (4,'Avek'),(5,'Brel'),(6,'Crix'),(7,'Daro'),(8,'Evek'),(9,'Farek'),(10,'Grix'),
 (11,'Horan'),(12,'Ivek'),(13,'Jaxa'),(14,'Krell'),(15,'Lorsa'),(16,'Mog'),
 (17,'Narek'),(18,'Odoz'),(19,'Prax'),(20,'Quilla'),(21,'Riva'),(22,'Sarek'),
 (23,'Torga'),(24,'Ulek'),(25,'Wex'),(26,'Xala'),(27,'Yorin'),(28,'Zek'),
 (29,'Arix'),(30,'Boran'),(31,'Cevik'),(32,'Drex'),(33,'Elix'),(34,'Fara'),
 (35,'Goran'),(36,'Hava'),(37,'Irix'),(38,'Jora'),(39,'Kava'),(40,'Luro'),
 (41,'Mira'),(42,'Naxa'),(43,'Orik'),(44,'Pela'),(45,'Rax'),(46,'Sula'),
 (47,'Tavin'),(48,'Urix'),(49,'Vara'),(50,'Woran'),(51,'Xev'),(52,'Yava'),
 (53,'Zorin'),(54,'Aldek'),(55,'Brava'),(56,'Corda'),(57,'Dovik'),(58,'Elar'),
 (59,'Fovan'),(60,'Garek'),(61,'Hovik'),(62,'Ivara'),(63,'Jarek'),(64,'Korin'),
 (65,'Lavek'),(66,'Marek'),(67,'Navor'),(68,'Ovek'),(69,'Parek'),(70,'Qorin'),
 (71,'Ravek'),(72,'Soran'),(73,'Tarek'),(74,'Uvara'),(75,'Vorek'),(76,'Warek'),
 (77,'Xorin'),(78,'Yarek'),(79,'Zavor'),(80,'Arvek'),(81,'Bavor'),(82,'Carek'),
 (83,'Darek'),(84,'Evor'),(85,'Fozik'),(86,'Gavor'),(87,'Harek'),(88,'Ivor'),
 (89,'Javor'),(90,'Karek'),(91,'Lavor'),(92,'Mavor'),(93,'Nerak'),(94,'Ovar'),
 (95,'Pavor'),(96,'Qarek'),(97,'Ravor'),(98,'Sovara'),(99,'Tavor'),(100,'Urek'),
 (101,'Varek'),(102,'Wavor'),(103,'Xarek'),(104,'Yavor'),(105,'Zarek'),
 (106,'Aldor'),(107,'Bexor'),(108,'Caldor'),(109,'Drexar'),(110,'Eldek'),
 (111,'Fexar'),(112,'Galdor'),(113,'Hedek'),(114,'Ildor'),(115,'Jexar'),
 (116,'Keldor'),(117,'Lexta'),(118,'Meldor'),(119,'Nexar'),(120,'Oldek'),
 (121,'Peldor'),(122,'Qexar'),(123,'Reldor'),(124,'Sexta'),(125,'Teldor'),
 (126,'Uexar'),(127,'Veldor'),(128,'Wexta'),(129,'Xeldor'),(130,'Yexar'),
 (131,'Zeldor'),(132,'Avolek'),(133,'Braxen'),(134,'Cevora'),(135,'Draxen'),
 (136,'Evorak'),(137,'Fraxen'),(138,'Gevora'),(139,'Hraxen'),(140,'Ivorak'),
 (141,'Jevora'),(142,'Kraxen'),(143,'Levor'),(144,'Mraxen'),(145,'Nevora'),
 (146,'Oraxen'),(147,'Pevor'),(148,'Qraxen'),(149,'Revor'),(150,'Sraxen'),
 (151,'Tevora'),(152,'Uraxen'),(153,'Vevor'),(154,'Wraxen'),(155,'Xevora'),
 (156,'Yraxen'),(157,'Zevor'),(158,'Avekta'),(159,'Brelak'),(160,'Crixel'),
 (161,'Davori'),(162,'Evekta'),(163,'Farekai'),(164,'Grixel'),(165,'Horanek'),
 (166,'Ivekta'),(167,'Jaxel'),(168,'Krellak'),(169,'Lorsai'),(170,'Mogek'),
 (171,'Nareki'),(172,'Odozak'),(173,'Praxen'),(174,'Quillak'),(175,'Rivak'),
 (176,'Sareki'),(177,'Torgak'),(178,'Uleki'),(179,'Wexel'),(180,'Xalak'),
 (181,'Yorinek'),(182,'Zekar'),(183,'Arik'),(184,'Borak'),(185,'Cavik'),
 (186,'Drexek'),(187,'Elvik'),(188,'Forak'),(189,'Gorak'),(190,'Havik'),
 (191,'Irixel'),(192,'Jorak'),(193,'Kavik'),(194,'Lurek'),(195,'Mirak'),
 (196,'Naxel'),(197,'Orak'),(198,'Pelak'),(199,'Raxel'),(200,'Sulak'),
 (201,'Tavik'),(202,'Urixel'),(203,'Varak'),(204,'Worak'),(205,'Xevik'),
 (206,'Yavik'),(207,'Zorak'),(208,'Aldekar'),(209,'Braxik'),(210,'Cordak'),
 (211,'Dovarek'),(212,'Elarek'),(213,'Fovik'),(214,'Gareki'),(215,'Hovarek'),
 (216,'Ivarak'),(217,'Jareki'),(218,'Korak'),(219,'Lavik'),(220,'Mareki'),
 (221,'Navarek'),(222,'Ovekai'),(223,'Pareki'),(224,'Qorak'),(225,'Raveki'),
 (226,'Sorak'),(227,'Tareki'),(228,'Uvarak'),(229,'Voreki'),(230,'Waren'),
 (231,'Xorak'),(232,'Yareki'),(233,'Zavarek'),(234,'Arovek'),(235,'Boreki'),
 (236,'Cavarek'),(237,'Doreki'),(238,'Evarek'),(239,'Foraki'),(240,'Goreki'),
 (241,'Havarek'),(242,'Ioreki'),(243,'Javarek'),(244,'Koreki'),(245,'Lavarek'),
 (246,'Moreki'),(247,'Navori'),(248,'Oraxik'),(249,'Poreki'),(250,'Qavarek');

WITH existing AS (
    SELECT d.trader_code, row_number() OVER (ORDER BY d.trader_code)::integer AS position
    FROM ferengi_trader_definitions d
    WHERE d.trader_code NOT IN ('gaila','moogie','vek')
), names AS (
    SELECT position, display_name FROM ferengi_v3_names WHERE position > 3
)
UPDATE ferengi_trader_definitions d
SET display_name = n.display_name, updated_at = CURRENT_TIMESTAMP
FROM existing e JOIN names n ON n.position = e.position + 3
WHERE d.trader_code = e.trader_code;

INSERT INTO ferengi_trader_definitions (trader_code, display_name, ship_type_id)
SELECT 'ferengi_v3_' || lpad(n.position::text, 3, '0'), n.display_name,
       (SELECT shiptypes_id FROM shiptypes WHERE name='Ferengi Trade Master')
FROM ferengi_v3_names n
WHERE n.position > 3
  AND NOT EXISTS (SELECT 1 FROM ferengi_trader_definitions d
                  WHERE d.display_name = n.display_name);

WITH ranked AS (
    SELECT d.trader_code, row_number() OVER (
             ORDER BY EXISTS (SELECT 1 FROM ferengi_traders t
                              WHERE t.trader_code=d.trader_code) DESC,
                      d.trader_code) AS rn,
           count(*) OVER () AS total
    FROM ferengi_trader_definitions d WHERE d.active=TRUE
), classes AS (
    SELECT
      max(shiptypes_id) FILTER (WHERE name='Ferengi Trade Master') AS trade_master,
      max(shiptypes_id) FILTER (WHERE name='Ferengi Cruiser') AS cruiser,
      max(shiptypes_id) FILTER (WHERE name='Ferengi Interceptor') AS interceptor,
      max(shiptypes_id) FILTER (WHERE name='Ferengi Warship') AS warship,
      max(shiptypes_id) FILTER (WHERE name='Ferengi Dreadnought') AS dreadnought
    FROM shiptypes
)
UPDATE ferengi_trader_definitions d
SET ship_type_id = CASE
    WHEN r.rn <= ceil(r.total * 40 / 100.0) THEN c.trade_master
    WHEN r.rn <= ceil(r.total * 70 / 100.0) THEN c.cruiser
    WHEN r.rn <= ceil(r.total * 86 / 100.0) THEN c.interceptor
    WHEN r.rn <= ceil(r.total * 96 / 100.0) THEN c.warship
    ELSE c.dreadnought END,
    updated_at = CURRENT_TIMESTAMP
FROM ranked r CROSS JOIN classes c
WHERE d.trader_code=r.trader_code;

-- Definition cargo is adjusted only for traders that do not yet have a live
-- ship.  This keeps new Interceptors within their 35-hold limit while leaving
-- every existing trader's cargo untouched.
WITH profiles AS (
    SELECT d.trader_code, st.maxholds,
           COALESCE(sum(c.quantity),0) AS total
    FROM ferengi_trader_definitions d
    JOIN shiptypes st ON st.shiptypes_id=d.ship_type_id
    LEFT JOIN ferengi_trader_definition_cargo c USING (trader_code)
    WHERE NOT EXISTS (SELECT 1 FROM ferengi_traders t
                      WHERE t.trader_code=d.trader_code)
    GROUP BY d.trader_code, st.maxholds
)
UPDATE ferengi_trader_definition_cargo c
SET quantity = floor(c.quantity * p.maxholds / NULLIF(p.total,0))::integer
FROM profiles p
WHERE c.trader_code=p.trader_code AND p.total > p.maxholds;

WITH desired AS (
    SELECT t.ship_id, d.ship_type_id
    FROM ferengi_traders t JOIN ferengi_trader_definitions d USING (trader_code)
    WHERE t.active=TRUE
), cargo AS (
    SELECT ship_id, COALESCE(sum(quantity),0) AS quantity
    FROM ship_cargo GROUP BY ship_id
)
UPDATE ships s SET type_id=desired.ship_type_id
FROM desired JOIN shiptypes st ON st.shiptypes_id=desired.ship_type_id
LEFT JOIN cargo cg ON cg.ship_id=desired.ship_id
WHERE s.ship_id=desired.ship_id
  AND COALESCE(cg.quantity,0) <= COALESCE(st.maxholds,0)
  AND COALESCE(s.fighters,0) <= COALESCE(st.maxfighters,0)
  AND COALESCE(s.mines,0) <= COALESCE(st.maxmines,0)
  AND COALESCE(s.shields,0) <= COALESCE(st.maxshields,0);

CREATE TEMP TABLE orion_v3_names (position integer PRIMARY KEY, display_name text NOT NULL) ON COMMIT DROP;
INSERT INTO orion_v3_names(position, display_name) VALUES
 (1,'Zydras Heavy Cruiser'),(2,'Krell Scout'),(3,'Vex Contraband Runner'),
 (4,'Jaxx Smuggler'),(5,'Sira Market Guard'),(6,'Dravik'),(7,'Korran'),
 (8,'Meyra'),(9,'Nexor'),(10,'Praxus'),(11,'Rellak'),(12,'Sovek'),
 (13,'Tavren'),(14,'Urix'),(15,'Varnak'),(16,'Wexar'),(17,'Xeran'),
 (18,'Yavik'),(19,'Zorren'),(20,'Akris'),(21,'Brakka'),(22,'Cevran'),
 (23,'Draxen'),(24,'Eryx'),(25,'Falkor'),(26,'Gavren'),(27,'Haxik'),
 (28,'Ivrak'),(29,'Jorven'),(30,'Kavrax'),(31,'Lyrak'),(32,'Mordek'),
 (33,'Nyris'),(34,'Orrak'),(35,'Pevran'),(36,'Qorvik'),(37,'Raxus'),
 (38,'Seryn'),(39,'Tavik'),(40,'Ulvex'),(41,'Veyran'),(42,'Worrik'),
 (43,'Xavren'),(44,'Yorrak'),(45,'Zevik'),(46,'Arvex'),(47,'Braxor'),
 (48,'Cyrak'),(49,'Dovren'),(50,'Evrik'),(51,'Fyran'),(52,'Gorran'),
 (53,'Havrex'),(54,'Ilyx'),(55,'Javren'),(56,'Korrik'),(57,'Lazren'),
 (58,'Mavik'),(59,'Norex'),(60,'Ovrak'),(61,'Pyran'),(62,'Qavik'),
 (63,'Rovex'),(64,'Saxren'),(65,'Torrik'),(66,'Uvrak'),(67,'Vaxen'),
 (68,'Wyrak'),(69,'Xorran'),(70,'Yevik'),(71,'Zavren'),(72,'Aldrax'),
 (73,'Borex'),(74,'Cavren'),(75,'Dyrak'),(76,'Eldran'),(77,'Fovex'),
 (78,'Graxen'),(79,'Horyx'),(80,'Ivrin'),(81,'Jaxor'),(82,'Keldra'),
 (83,'Lornak'),(84,'Meyvik'),(85,'Naxor'),(86,'Orren'),(87,'Praxik'),
 (88,'Qeldor'),(89,'Ravex'),(90,'Sornak'),(91,'Teyra'),(92,'Uldrik'),
 (93,'Vorran'),(94,'Wexik'),(95,'Xeldra'),(96,'Yvrak'),(97,'Zornex'),
 (98,'Averik'),(99,'Braxen'),(100,'Coryx');

WITH generated AS (
    SELECT player_id, row_number() OVER (ORDER BY player_id)::integer AS position
    FROM players WHERE is_npc=TRUE AND name LIKE 'Orion Captain %'
), names AS (
    SELECT position, display_name FROM orion_v3_names WHERE position > 5
)
UPDATE players p SET name=n.display_name
FROM generated g JOIN names n ON n.position=g.position + 5
WHERE p.player_id=g.player_id
  AND NOT EXISTS (SELECT 1 FROM players other
                  WHERE other.name=n.display_name AND other.player_id<>p.player_id);

INSERT INTO players (name, passwd, sector_id, credits, type, is_npc, commission_id)
SELECT n.display_name, '$argon2id$v=19$m=65536,t=3,p=1$m9e+qkJe5du1SC25jkIB0A$fd2PpiLEjU5d2AXlAf1nznGko41GPU7Z2jxX3gX0QZE',
       COALESCE((SELECT center_sector FROM clusters WHERE name='Orion Syndicate' LIMIT 1), 1),
       100000, 2, TRUE, 1
FROM orion_v3_names n
WHERE n.position > 5
  AND NOT EXISTS (SELECT 1 FROM players p WHERE p.name=n.display_name AND p.is_npc=TRUE);
INSERT INTO corp_members (corporation_id, player_id, role)
SELECT c.corporation_id, p.player_id, 'Officer'
FROM corporations c CROSS JOIN players p
WHERE c.tag='ORION' AND p.is_npc=TRUE
  AND p.name IN (SELECT display_name FROM orion_v3_names)
ON CONFLICT DO NOTHING;

COMMIT;
