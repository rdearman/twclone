#ifndef SERVER_BIGBANG_H
#define SERVER_BIGBANG_H

/* Initialise and populate the universe if missing */
int bigbang(void);

/* Internal helpers */
int create_sectors(int numSectors);
int create_fedspace(void);
int create_ports(int numPorts);
int create_ferringhi(void);
int create_planets(int numPlanets);

#endif /* SERVER_BIGBANG_H */
