#ifndef IWPNHACK_H
#define IWPNHACK_H

#pragma once

#include <IShareSys.h>

#define SMINTERFACE_WPNHACK_NAME "IWpnHack"
#define SMINTERFACE_WPNHACK_VERSION 1

class IAmmoDef
{
public:
	virtual int PlrDamage(int nAmmoIndex) = 0;
	virtual int NPCDamage(int nAmmoIndex) = 0;
	virtual int DamageType(int nAmmoIndex) = 0;
	virtual float DamageForce(int nAmmoIndex) = 0;
	virtual int Index(const char *name) = 0;
};

class IWpnHack : public SourceMod::SMInterface
{
public:
	virtual const char *GetInterfaceName()
	{ return SMINTERFACE_WPNHACK_NAME; }
	virtual unsigned int GetInterfaceVersion()
	{ return SMINTERFACE_WPNHACK_VERSION; }

	virtual IAmmoDef *AmmoDef() = 0;
};

#endif
