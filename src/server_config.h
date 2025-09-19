#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

struct twconfig
{
  int turnsperday;
  int maxwarps_per_sector;
  int startingcredits;
  int startingfighters;
  int startingholds;
  int processinterval;
  int autosave;
  int max_players;
  int max_ships;
  int max_ports;
  int max_planets_per_sector;
  int max_total_planets;
  int max_safe_planets;
  int max_citadel_level;
  int number_of_planet_types;
  int max_ship_name_length;
  int ship_type_count;
  int hash_length;
  int default_port;
  int default_nodes;
  int warps_per_sector;
  int buff_size;
  int max_name_length;
  int planet_type_count;
};

struct twconfig *config_load (void);
void initconfig (void);

#endif /* CONFIG_H */
