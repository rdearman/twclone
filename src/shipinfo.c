#include <string.h>
#include <stdio.h>

#include "shipinfo.h"
#include "common.h"
#include "config.h"

extern struct config *configdata;
extern struct sp_shipinfo **shiptypes;

void saveshiptypeinfo(char *filename)
{
	FILE *shipfile;
	char stufftosave[BUFF_SIZE];
	int index=0;
	int len=0;
	int loop;

	shipfile = fopen(filename, "w");

	for (index=0; index < configdata->ship_type_count; index++)
	{
		strcpy(stufftosave, "\0");
		addstring(stufftosave, shiptypes[index]->name, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->basecost, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->maxattack, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->initialholds, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->maxholds, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->maxfighters, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->turns, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->mines, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->genesis, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->twarp, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->transportrange, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->maxshields, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->offense, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->defense, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->beacons, ':', BUFF_SIZE);
 		addint(stufftosave, shiptypes[index]->holo, ':', BUFF_SIZE);
		addint(stufftosave, shiptypes[index]->planet, ':', BUFF_SIZE);
 		addint(stufftosave, shiptypes[index]->photons, ':', BUFF_SIZE);
		
		len = strlen(stufftosave);
		for (loop=1; loop <= 300 - len; loop++)
			strcat(stufftosave, " ");
		strcat(stufftosave, "\n");
		fprintf(shipfile, "%s", stufftosave);
	}
	fclose(shipfile);
}

void init_shiptypeinfo (char *filename)
{
	int index=0;
	FILE *shipfile=NULL;
	char *buffer;
	int done=0;

	buffer = (char *)malloc(sizeof(char)*BUFF_SIZE);
	if (buffer==NULL)
	{
		fprintf(stderr, "init_shiptypeinfo: Can't allocate mem for buffer!");
	}
	shiptypes = (struct sp_shipinfo **)
			  malloc(sizeof(struct sp_shipinfo *)*configdata->ship_type_count);
	shipfile = fopen(filename, "r");
	if (shipfile==NULL)
	{
		fprintf(stderr, "\ninit_shiptypeinfo: No shipinfo file!");
		return;
	}
	
	while(!done)
	{
		strcpy(buffer, "\0");
		fgets(buffer, BUFF_SIZE, shipfile);
		if (index >= configdata->ship_type_count)
			done=1;
		if (strlen(buffer)==0)
			done=1;
		else if (index < configdata->ship_type_count)
		{
			shiptypes[index] = 
				(struct sp_shipinfo *)malloc(sizeof(struct sp_shipinfo));
			if (shiptypes[index] != NULL)
			{
			popstring(buffer, shiptypes[index]->name, ":", BUFF_SIZE);
			shiptypes[index]->basecost = popint(buffer, ":");
			shiptypes[index]->maxattack = popint(buffer, ":");
			shiptypes[index]->initialholds = popint(buffer, ":");
			shiptypes[index]->maxholds = popint(buffer, ":");
			shiptypes[index]->maxfighters = popint(buffer, ":");
			shiptypes[index]->turns = popint(buffer, ":");
			shiptypes[index]->mines = popint(buffer, ":");
			shiptypes[index]->genesis = popint(buffer, ":");
			shiptypes[index]->twarp = popint(buffer, ":");
			shiptypes[index]->transportrange = popint(buffer, ":");
			shiptypes[index]->maxshields = popint(buffer, ":");
			shiptypes[index]->offense = popint(buffer, ":");
			shiptypes[index]->defense = popint(buffer, ":");
			shiptypes[index]->beacons = popint(buffer, ":");
			shiptypes[index]->holo = popint(buffer, ":");
			shiptypes[index]->planet = popint(buffer, ":");
			shiptypes[index]->photons = popint(buffer, ":");
			index++;
			}
		}
	}
	fclose(shipfile);
	free(buffer);
		  
		  /*  strcpy (shiptypes[0].name, "\x1B[0;32mMerchant Cruiser\x1B[0m");
  shiptypes[0].basecost = 41300;
  shiptypes[0].maxattack = 750;
  shiptypes[0].initialholds = 20;
  shiptypes[0].maxholds = 75;
  shiptypes[0].maxfighters = 2500;
  shiptypes[0].turns = 3;
  shiptypes[0].mines = 50;
  shiptypes[0].genesis = 5;
  shiptypes[0].twarp = 0;
  shiptypes[0].transportrange = 5;
  shiptypes[0].maxshields = 400;
  shiptypes[0].offense = 10;
  shiptypes[0].defense = 10;
  shiptypes[0].beacons = 0;	/////I forgot to fill this in
  shiptypes[0].holo = 1;
  shiptypes[0].planet = 1;
  shiptypes[0].photons = 0;

  strcpy (shiptypes[1].name, "\x1B[0;35mScout Marauder\x1B[0m");
  shiptypes[1].basecost = 15950;
  shiptypes[1].maxattack = 250;
  shiptypes[1].initialholds = 10;
  shiptypes[1].maxholds = 25;
  shiptypes[1].maxfighters = 250;
  shiptypes[1].turns = 2;
  shiptypes[1].mines = 0;
  shiptypes[1].genesis = 0;
  shiptypes[1].twarp = 0;
  shiptypes[1].transportrange = 0;
  shiptypes[1].maxshields = 100;
  shiptypes[1].offense = 20;
  shiptypes[1].defense = 20;
  shiptypes[1].beacons = 0;	/////I forgot to fill this in
  shiptypes[1].holo = 1;
  shiptypes[1].planet = 1;
  shiptypes[1].photons = 0;

  strcpy (shiptypes[2].name, "\x1B[0;33mMissile Frigate\x1B[0m");
  shiptypes[2].basecost = 100000;
  shiptypes[2].maxattack = 2000;
  shiptypes[2].initialholds = 12;
  shiptypes[2].maxholds = 60;
  shiptypes[2].maxfighters = 5000;
  shiptypes[2].turns = 3;
  shiptypes[2].mines = 5;
  shiptypes[2].genesis = 0;
  shiptypes[2].twarp = 0;
  shiptypes[2].transportrange = 2;
  shiptypes[2].maxshields = 400;
  shiptypes[2].offense = 13;
  shiptypes[2].defense = 13;
  shiptypes[2].beacons = 5;
  shiptypes[2].holo = 0;
  shiptypes[2].planet = 0;
  shiptypes[2].photons = 1;

  strcpy (shiptypes[3].name, "\x1B[1;33mBattleship\x1B[0m");
  shiptypes[3].basecost = 88500;
  shiptypes[3].maxattack = 3000;
  shiptypes[3].initialholds = 16;
  shiptypes[3].maxholds = 80;
  shiptypes[3].maxfighters = 10000;
  shiptypes[3].turns = 4;
  shiptypes[3].mines = 25;
  shiptypes[3].genesis = 1;
  shiptypes[3].twarp = 0;
  shiptypes[3].transportrange = 8;
  shiptypes[3].maxshields = 750;
  shiptypes[3].offense = 16;
  shiptypes[3].defense = 16;
  shiptypes[3].beacons = 50;
  shiptypes[3].holo = 1;
  shiptypes[3].planet = 1;
  shiptypes[3].photons = 0;

  strcpy (shiptypes[4].name, "\x1B[0;31mCorporate Flagship\x1B[0m");
  shiptypes[4].basecost = 163500;
  shiptypes[4].maxattack = 6000;
  shiptypes[4].initialholds = 20;
  shiptypes[4].maxholds = 85;
  shiptypes[4].maxfighters = 20000;
  shiptypes[4].turns = 3;
  shiptypes[4].mines = 100;
  shiptypes[4].genesis = 10;
  shiptypes[4].twarp = 1;
  shiptypes[4].transportrange = 10;
  shiptypes[4].maxshields = 1500;
  shiptypes[4].offense = 12;
  shiptypes[4].defense = 12;
  shiptypes[4].beacons = 100;
  shiptypes[4].holo = 1;
  shiptypes[4].planet = 1;
  shiptypes[4].photons = 0;

  strcpy (shiptypes[5].name, "\x1B[0;36mColonial Transport\x1B[0m");
  shiptypes[5].basecost = 63600;
  shiptypes[5].maxattack = 100;
  shiptypes[5].initialholds = 50;
  shiptypes[5].maxholds = 250;
  shiptypes[5].maxfighters = 200;
  shiptypes[5].turns = 6;
  shiptypes[5].mines = 0;
  shiptypes[5].genesis = 5;
  shiptypes[5].twarp = 0;
  shiptypes[5].transportrange = 7;
  shiptypes[5].maxshields = 500;
  shiptypes[5].offense = 6;
  shiptypes[5].defense = 6;
  shiptypes[5].beacons = 10;
  shiptypes[5].holo = 0;
  shiptypes[5].planet = 1;
  shiptypes[5].photons = 0;

  strcpy (shiptypes[6].name, "\x1B[1;34mCargo Transport\x1B[0m");
  shiptypes[6].basecost = 51950;
  shiptypes[6].maxattack = 125;
  shiptypes[6].initialholds = 50;
  shiptypes[6].maxholds = 125;
  shiptypes[6].maxfighters = 400;
  shiptypes[6].turns = 4;
  shiptypes[6].mines = 1;
  shiptypes[6].genesis = 2;
  shiptypes[6].twarp = 0;
  shiptypes[6].transportrange = 5;
  shiptypes[6].maxshields = 1000;
  shiptypes[6].offense = 8;
  shiptypes[6].defense = 8;
  shiptypes[6].beacons = 20;
  shiptypes[6].holo = 1;
  shiptypes[6].planet = 1;
  shiptypes[6].photons = 0;

  strcpy (shiptypes[7].name, "\x1B[1;32mMerchant Freighter\x1B[0m");
  shiptypes[7].basecost = 33400;
  shiptypes[7].maxattack = 100;
  shiptypes[7].initialholds = 30;
  shiptypes[7].maxholds = 65;
  shiptypes[7].maxfighters = 300;
  shiptypes[7].turns = 2;
  shiptypes[7].mines = 2;
  shiptypes[7].genesis = 2;
  shiptypes[7].twarp = 0;
  shiptypes[7].transportrange = 5;
  shiptypes[7].maxshields = 500;
  shiptypes[7].offense = 8;
  shiptypes[7].defense = 8;
  shiptypes[7].beacons = 20;
  shiptypes[7].holo = 1;
  shiptypes[7].planet = 1;
  shiptypes[7].photons = 0;

  strcpy (shiptypes[8].name, "\x1B[7;34;47mImperial Starship\x1B[0m");
  shiptypes[8].basecost = 329000;
  shiptypes[8].maxattack = 10000;
  shiptypes[8].initialholds = 40;
  shiptypes[8].maxholds = 150;
  shiptypes[8].maxfighters = 50000;
  shiptypes[8].turns = 4;
  shiptypes[8].mines = 125;
  shiptypes[8].genesis = 10;
  shiptypes[8].twarp = 1;
  shiptypes[8].transportrange = 15;
  shiptypes[8].maxshields = 2000;
  shiptypes[8].offense = 15;
  shiptypes[8].defense = 15;
  shiptypes[8].beacons = 150;
  shiptypes[8].holo = 1;
  shiptypes[8].planet = 1;
  shiptypes[8].photons = 1;

  strcpy (shiptypes[9].name, "\x1B[1;35mHavoc Gunstar\x1B[0m");
  shiptypes[9].basecost = 79000;
  shiptypes[9].maxattack = 1000;
  shiptypes[9].initialholds = 12;
  shiptypes[9].maxholds = 50;
  shiptypes[9].maxfighters = 10000;
  shiptypes[9].turns = 3;
  shiptypes[9].mines = 5;
  shiptypes[9].genesis = 1;
  shiptypes[9].twarp = 1;
  shiptypes[9].transportrange = 6;
  shiptypes[9].maxshields = 3000;
  shiptypes[9].offense = 13;
  shiptypes[9].defense = 13;
  shiptypes[9].beacons = 5;
  shiptypes[9].holo = 1;
  shiptypes[9].planet = 0;
  shiptypes[9].photons = 0;

  strcpy (shiptypes[10].name, "\x1B[7;39;44mConstellation\x1B[0m");
  shiptypes[10].basecost = 72500;
  shiptypes[10].maxattack = 2000;
  shiptypes[10].initialholds = 20;
  shiptypes[10].maxholds = 80;
  shiptypes[10].maxfighters = 5000;
  shiptypes[10].turns = 3;
  shiptypes[10].mines = 25;
  shiptypes[10].genesis = 2;
  shiptypes[10].twarp = 0;
  shiptypes[10].transportrange = 6;
  shiptypes[10].maxshields = 750;
  shiptypes[10].offense = 14;
  shiptypes[10].defense = 14;
  shiptypes[10].beacons = 50;
  shiptypes[10].holo = 1;
  shiptypes[10].planet = 1;
  shiptypes[10].photons = 0;

  strcpy (shiptypes[11].name, "\x1B[1;31mT'khasi Orion\x1B[0m");
  shiptypes[11].basecost = 42500;
  shiptypes[11].maxattack = 250;
  shiptypes[11].initialholds = 30;
  shiptypes[11].maxholds = 60;
  shiptypes[11].maxfighters = 750;
  shiptypes[11].turns = 2;
  shiptypes[11].mines = 5;
  shiptypes[11].genesis = 1;
  shiptypes[11].twarp = 0;
  shiptypes[11].transportrange = 3;
  shiptypes[11].maxshields = 750;
  shiptypes[11].offense = 11;
  shiptypes[11].defense = 11;
  shiptypes[11].beacons = 20;
  shiptypes[11].holo = 1;
  shiptypes[11].planet = 1;
  shiptypes[11].photons = 0;

  strcpy (shiptypes[12].name, "\x1B[7;34;1;43mTholian Sentinel\x1B[0m");
  shiptypes[12].basecost = 47500;
  shiptypes[12].maxattack = 800;
  shiptypes[12].initialholds = 10;
  shiptypes[12].maxholds = 50;
  shiptypes[12].maxfighters = 2500;
  shiptypes[12].turns = 4;
  shiptypes[12].mines = 50;
  shiptypes[12].genesis = 1;
  shiptypes[12].twarp = 0;
  shiptypes[12].transportrange = 3;
  shiptypes[12].maxshields = 4000;
  shiptypes[12].offense = 1;
  shiptypes[12].defense = 1;
  shiptypes[12].beacons = 10;
  shiptypes[12].holo = 1;
  shiptypes[12].planet = 0;
  shiptypes[12].photons = 0;

  strcpy (shiptypes[13].name, "\x1B[7;39;41mTaurean Mule\x1B[0m");
  shiptypes[13].basecost = 63600;
  shiptypes[13].maxattack = 150;
  shiptypes[13].initialholds = 50;
  shiptypes[13].maxholds = 150;
  shiptypes[13].maxfighters = 300;
  shiptypes[13].turns = 4;
  shiptypes[13].mines = 0;
  shiptypes[13].genesis = 1;
  shiptypes[13].twarp = 0;
  shiptypes[13].transportrange = 5;
  shiptypes[13].maxshields = 600;
  shiptypes[13].offense = 5;
  shiptypes[13].defense = 5;
  shiptypes[13].beacons = 20;
  shiptypes[13].holo = 1;
  shiptypes[13].planet = 1;
  shiptypes[13].photons = 0;

  strcpy (shiptypes[14].name, "\x1B[7;31;49mInterdictor Cruiser\x1B[0m");
  shiptypes[14].basecost = 539000;
  shiptypes[14].maxattack = 15000;
  shiptypes[14].initialholds = 10;
  shiptypes[14].maxholds = 40;
  shiptypes[14].maxfighters = 100000;
  shiptypes[14].turns = 15;
  shiptypes[14].mines = 200;
  shiptypes[14].genesis = 20;
  shiptypes[14].twarp = 0;
  shiptypes[14].transportrange = 20;
  shiptypes[14].maxshields = 4000;
  shiptypes[14].offense = 12;
  shiptypes[14].defense = 12;
  shiptypes[14].beacons = 100;
  shiptypes[14].holo = 1;
  shiptypes[14].planet = 1;
  shiptypes[14].photons = 0;*/

  return;
}
