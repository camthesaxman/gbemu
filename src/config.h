#ifndef GUARD_CONFIG_H
#define GUARD_CONFIG_H

#define CONFIG_FILE_NAME "gbemu_cfg.txt"

struct ConfigKeys
{
	unsigned int a;
	unsigned int b;
	unsigned int start;
	unsigned int select;
	unsigned int up;
	unsigned int down;
	unsigned int left;
	unsigned int right;
    unsigned int fastFwd;
};

struct Config
{
	unsigned int windowWidth;
	unsigned int windowHeight;
#ifdef FRONTEND_WINDOWS
	bool showMenuBar;
	bool fixedAspectRatio;
	bool snapWindowSize;
    unsigned int colorPalette;
#endif
	struct ConfigKeys keys;
};

extern struct Config gConfig;

void config_load(const char *filename);
void config_save(const char *filename);

#endif  // GUARD_CONFIG_H
