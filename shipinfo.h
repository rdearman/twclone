#ifndef SHIP_INFO_H
#define SHIP_INFO_H

#define SHIP_TYPE_COUNT 15
#define MAX_SHIP_NAME_LENGTH 40

struct sp_shipinfo
{
  char name[MAX_SHIP_NAME_LENGTH];
  int basecost;
  int maxattack;
  int initialholds;
  int maxholds;
  int maxfighters;
  int turns;
  int mines;
  int genesis;
  unsigned short twarp;
  int transportrange;
  int maxshields;
  int offense;			//This needs to be divided by 10 before use 
  int defense;			//This needs to be divided by 10 before use 
  int beacons;
  unsigned short holo;
  unsigned short planet;
  unsigned short photons;
};

void init_shiptypeinfo (char *filename);
void saveshiptypeinfo(char *filename);

#endif
