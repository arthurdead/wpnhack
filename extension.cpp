/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Sample Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include <utility>
#include <unordered_map>
#include <string>
#include <string_view>

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

#include "extension.h"
#include <tier1/utldict.h>
#include <ISDKTools.h>
#include <vstdlib/cvar.h>
#include <tier1/convar.h>
#include <filesystem.h>
#include <CDetour/detours.h>
#include <ISDKHooks.h>
#include <shareddefs.h>

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

Sample g_Sample;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_Sample);

static ISDKHooks *g_pSDKHooks{nullptr};

typedef unsigned short WEAPON_FILE_INFO_HANDLE;

struct FileWeaponInfo_t;
class IFileSystem;

template <typename R, typename T, typename ...Args>
R call_mfunc(T *pThisPtr, void *offset, Args ...args)
{
	class VEmptyClass {};
	
	void **this_ptr = *reinterpret_cast<void ***>(&pThisPtr);
	
	union
	{
		R (VEmptyClass::*mfpnew)(Args...);
#ifndef PLATFORM_POSIX
		void *addr;
	} u;
	u.addr = offset;
#else
		struct  
		{
			void *addr;
			intptr_t adjustor;
		} s;
	} u;
	u.s.addr = offset;
	u.s.adjustor = 0;
#endif
	
	return (R)(reinterpret_cast<VEmptyClass *>(this_ptr)->*u.mfpnew)(args...);
}

template <typename R, typename T, typename ...Args>
R call_vfunc(T *pThisPtr, size_t offset, Args ...args)
{
	void **vtable = *reinterpret_cast<void ***>(pThisPtr);
	void *vfunc = vtable[offset];
	
	return call_mfunc<R, T, Args...>(pThisPtr, vfunc, args...);
}

template <typename T>
T void_to_func(void *ptr)
{
	union { T f; void *p; };
	p = ptr;
	return f;
}

template <typename T>
void *func_to_void(T ptr)
{
	union { T f; void *p; };
	f = ptr;
	return p;
}

class CGameRules;

ISDKTools *g_pSDKTools = nullptr;

CGameRules *g_pGameRules;
int GetEncryptionKeyoffset;

IFileSystem *filesystem;

void *GetFileWeaponInfoFromHandleAddr;

void *FileWeaponInfo_tParseAddr;
void *CTFWeaponInfoParseAddr;

int CTFWeaponBase_m_pWeaponInfo_offset = -1;
int CTFWeaponBase_m_iWeaponMode_offset = -1;

struct Ammo_t 
{
	char 				*pName;
	int					nDamageType;
	int					eTracerType;
	float				physicsForceImpulse;
	int					nMinSplashSize;
	int					nMaxSplashSize;

	int					nFlags;

	// Values for player/NPC damage and carrying capability
	// If the integers are set, they override the CVars
	int					pPlrDmg;		// CVar for player damage amount
	int					pNPCDmg;		// CVar for NPC damage amount
	int					pMaxCarry;		// CVar for maximum number can carry
	const ConVar*		pPlrDmgCVar;	// CVar for player damage amount
	const ConVar*		pNPCDmgCVar;	// CVar for NPC damage amount
	const ConVar*		pMaxCarryCVar;	// CVar for maximum number can carry

	Ammo_t(const Ammo_t &other)
	{ operator=(other); }
	Ammo_t &operator=(const Ammo_t &other)
	{
		if(pName) {
			delete[] pName;
			pName = nullptr;
		}

		if(other.pName) {
			pName = new char[strlen(other.pName)+1];
			strcpy(pName, other.pName);
		}

		nDamageType = other.nDamageType;
		eTracerType = other.eTracerType;
		physicsForceImpulse = other.physicsForceImpulse;
		nMinSplashSize = other.nMinSplashSize;
		nMaxSplashSize = other.nMaxSplashSize;
		nFlags = other.nFlags;
		pPlrDmg = other.pPlrDmg;
		pNPCDmg = other.pNPCDmg;
		pMaxCarry = other.pMaxCarry;
		pPlrDmgCVar = other.pPlrDmgCVar;
		pNPCDmgCVar = other.pNPCDmgCVar;
		pMaxCarryCVar = other.pMaxCarryCVar;

		return *this;
	}

	Ammo_t(Ammo_t &&other)
	{ operator=(std::move(other)); }
	Ammo_t &operator=(Ammo_t &&other)
	{
		if(pName) {
			delete[] pName;
			pName = nullptr;
		}

		pName = other.pName;

		nDamageType = other.nDamageType;
		eTracerType = other.eTracerType;
		physicsForceImpulse = other.physicsForceImpulse;
		nMinSplashSize = other.nMinSplashSize;
		nMaxSplashSize = other.nMaxSplashSize;
		nFlags = other.nFlags;
		pPlrDmg = other.pPlrDmg;
		pNPCDmg = other.pNPCDmg;
		pMaxCarry = other.pMaxCarry;
		pPlrDmgCVar = other.pPlrDmgCVar;
		pNPCDmgCVar = other.pNPCDmgCVar;
		pMaxCarryCVar = other.pMaxCarryCVar;

		other.pName = new char[1]{'\0'};

		other.pPlrDmgCVar = nullptr;
		other.pNPCDmgCVar = nullptr;
		other.pMaxCarryCVar = nullptr;

		return *this;
	}
};

#define	MAX_AMMO_TYPES 32

#define USE_CVAR -1

struct CAmmoDef
{
	virtual ~CAmmoDef() = 0;

	int m_nAmmoIndex;
	Ammo_t m_AmmoType[MAX_AMMO_TYPES];

	int Index(const char *psz)
	{
		int i;

		if (!psz)
			return -1;

		for (i = 1; i < m_nAmmoIndex; i++)
		{
			if (stricmp( psz, m_AmmoType[i].pName ) == 0)
				return i;
		}

		return -1;
	}

	bool AddAmmoType(char const* name, int damageType, int tracerType, int nFlags, int minSplashSize, int maxSplashSize )
	{
		if (m_nAmmoIndex == MAX_AMMO_TYPES)
			return false;

		int len = strlen(name);
		m_AmmoType[m_nAmmoIndex].pName = new char[len+1];
		Q_strncpy(m_AmmoType[m_nAmmoIndex].pName, name,len+1);
		m_AmmoType[m_nAmmoIndex].nDamageType	= damageType;
		m_AmmoType[m_nAmmoIndex].eTracerType	= tracerType;
		m_AmmoType[m_nAmmoIndex].nMinSplashSize	= minSplashSize;
		m_AmmoType[m_nAmmoIndex].nMaxSplashSize	= maxSplashSize;
		m_AmmoType[m_nAmmoIndex].nFlags	= nFlags;

		return true;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Add an ammo type with it's damage & carrying capability specified via cvars
	//-----------------------------------------------------------------------------
	bool AddAmmoType(char const* name, int damageType, int tracerType, 
		char const* plr_cvar, char const* npc_cvar, char const* carry_cvar, 
		float physicsForceImpulse, int nFlags, int minSplashSize, int maxSplashSize)
	{
		if ( AddAmmoType( name, damageType, tracerType, nFlags, minSplashSize, maxSplashSize ) == false )
			return false;

		if (plr_cvar)
		{
			m_AmmoType[m_nAmmoIndex].pPlrDmgCVar	= cvar->FindVar(plr_cvar);
			if (!m_AmmoType[m_nAmmoIndex].pPlrDmgCVar)
			{
				Msg("ERROR: Ammo (%s) found no CVar named (%s)\n",name,plr_cvar);
			}
			m_AmmoType[m_nAmmoIndex].pPlrDmg = USE_CVAR;
		}
		if (npc_cvar)
		{
			m_AmmoType[m_nAmmoIndex].pNPCDmgCVar	= cvar->FindVar(npc_cvar);
			if (!m_AmmoType[m_nAmmoIndex].pNPCDmgCVar)
			{
				Msg("ERROR: Ammo (%s) found no CVar named (%s)\n",name,npc_cvar);
			}
			m_AmmoType[m_nAmmoIndex].pNPCDmg = USE_CVAR;
		}
		if (carry_cvar)
		{
			m_AmmoType[m_nAmmoIndex].pMaxCarryCVar= cvar->FindVar(carry_cvar);
			if (!m_AmmoType[m_nAmmoIndex].pMaxCarryCVar)
			{
				Msg("ERROR: Ammo (%s) found no CVar named (%s)\n",name,carry_cvar);
			}
			m_AmmoType[m_nAmmoIndex].pMaxCarry = USE_CVAR;
		}
		m_AmmoType[m_nAmmoIndex].physicsForceImpulse = physicsForceImpulse;
		m_nAmmoIndex++;
		return true;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Add an ammo type with it's damage & carrying capability specified via integers
	//-----------------------------------------------------------------------------
	bool AddAmmoType(char const* name, int damageType, int tracerType, 
		int plr_dmg, int npc_dmg, int carry, float physicsForceImpulse, 
		int nFlags, int minSplashSize, int maxSplashSize )
	{
		if ( AddAmmoType( name, damageType, tracerType, nFlags, minSplashSize, maxSplashSize ) == false )
			return false;

		m_AmmoType[m_nAmmoIndex].pPlrDmg = plr_dmg;
		m_AmmoType[m_nAmmoIndex].pNPCDmg = npc_dmg;
		m_AmmoType[m_nAmmoIndex].pMaxCarry = carry;
		m_AmmoType[m_nAmmoIndex].physicsForceImpulse = physicsForceImpulse;

		m_nAmmoIndex++;
		return true;
	}

	int	PlrDamage(int nAmmoIndex)
	{
		if ( nAmmoIndex < 1 || nAmmoIndex >= m_nAmmoIndex )
			return 0;

		if ( m_AmmoType[nAmmoIndex].pPlrDmg == USE_CVAR )
		{
			if ( m_AmmoType[nAmmoIndex].pPlrDmgCVar )
			{
				return m_AmmoType[nAmmoIndex].pPlrDmgCVar->GetFloat();
			}

			return 0;
		}
		else
		{
			return m_AmmoType[nAmmoIndex].pPlrDmg;
		}
	}

	int	NPCDamage(int nAmmoIndex)
	{
		if ( nAmmoIndex < 1 || nAmmoIndex >= m_nAmmoIndex )
			return 0;

		if ( m_AmmoType[nAmmoIndex].pNPCDmg == USE_CVAR )
		{
			if ( m_AmmoType[nAmmoIndex].pNPCDmgCVar )
			{
				return m_AmmoType[nAmmoIndex].pNPCDmgCVar->GetFloat();
			}

			return 0;
		}
		else
		{
			return m_AmmoType[nAmmoIndex].pNPCDmg;
		}
	}

	int	DamageType(int nAmmoIndex)
	{
		if (nAmmoIndex < 1 || nAmmoIndex >= m_nAmmoIndex)
			return 0;

		return m_AmmoType[nAmmoIndex].nDamageType;
	}
};

void *GetAmmoDefAddr;

int Sample::PlrDamage(int nAmmoIndex)
{ return (void_to_func<CAmmoDef *(*)()>(GetAmmoDefAddr))()->PlrDamage(nAmmoIndex); }
int Sample::NPCDamage(int nAmmoIndex)
{ return (void_to_func<CAmmoDef *(*)()>(GetAmmoDefAddr))()->NPCDamage(nAmmoIndex); }
int Sample::DamageType(int nAmmoIndex)
{ return (void_to_func<CAmmoDef *(*)()>(GetAmmoDefAddr))()->DamageType(nAmmoIndex); }

void *m_WeaponInfoDatabaseAddr;

class CHudTexture;

#define MAX_WEAPON_STRING 80
#define MAX_WEAPON_AMMO_NAME 32
#define MAX_WEAPON_PREFIX 16

typedef enum {
	EMPTY,
	SINGLE,
	SINGLE_NPC,
	WPN_DOUBLE, // Can't be "DOUBLE" because windows.h uses it.
	DOUBLE_NPC,
	BURST,
	RELOAD,
	RELOAD_NPC,
	MELEE_MISS,
	MELEE_HIT,
	MELEE_HIT_WORLD,
	SPECIAL1,
	SPECIAL2,
	SPECIAL3,
	TAUNT,
	DEPLOY,

	// Add new shoot sound types here

	NUM_SHOOT_SOUND_TYPES,
} WeaponSound_t;

struct FileWeaponInfo_t
{
public:
	virtual void Parse( KeyValues *pKeyValuesData, const char *szWeaponName )
	{
		call_mfunc<void, FileWeaponInfo_t, KeyValues *, const char *>(this, FileWeaponInfo_tParseAddr, pKeyValuesData, szWeaponName);
	}

	FileWeaponInfo_t()
	{
		bParsedScript = false;
		bLoadedHudElements = false;
		szClassName[0] = 0;
		szPrintName[0] = 0;

		szViewModel[0] = 0;
		szWorldModel[0] = 0;
		szAnimationPrefix[0] = 0;
		iSlot = 0;
		iPosition = 0;
		iMaxClip1 = 0;
		iMaxClip2 = 0;
		iDefaultClip1 = 0;
		iDefaultClip2 = 0;
		iWeight = 0;
		iRumbleEffect = -1;
		bAutoSwitchTo = false;
		bAutoSwitchFrom = false;
		iFlags = 0;
		szAmmo1[0] = 0;
		szAmmo2[0] = 0;
		memset( aShootSounds, 0, sizeof( aShootSounds ) );
		iAmmoType = 0;
		iAmmo2Type = 0;
		m_bMeleeWeapon = false;
		iSpriteCount = 0;
		iconActive = 0;
		iconInactive = 0;
		iconAmmo = 0;
		iconAmmo2 = 0;
		iconCrosshair = 0;
		iconAutoaim = 0;
		iconZoomedCrosshair = 0;
		iconZoomedAutoaim = 0;
		bShowUsageHint = false;
		m_bAllowFlipping = true;
		m_bBuiltRightHanded = true;
	}

	~FileWeaponInfo_t() = default;

	bool					bParsedScript;
	bool					bLoadedHudElements;

// SHARED
	char					szClassName[MAX_WEAPON_STRING];
	char					szPrintName[MAX_WEAPON_STRING];			// Name for showing in HUD, etc.

	char					szViewModel[MAX_WEAPON_STRING];			// View model of this weapon
	char					szWorldModel[MAX_WEAPON_STRING];		// Model of this weapon seen carried by the player
	char					szAnimationPrefix[MAX_WEAPON_PREFIX];	// Prefix of the animations that should be used by the player carrying this weapon
	int						iSlot;									// inventory slot.
	int						iPosition;								// position in the inventory slot.
	int						iMaxClip1;								// max primary clip size (-1 if no clip)
	int						iMaxClip2;								// max secondary clip size (-1 if no clip)
	int						iDefaultClip1;							// amount of primary ammo in the gun when it's created
	int						iDefaultClip2;							// amount of secondary ammo in the gun when it's created
	int						iWeight;								// this value used to determine this weapon's importance in autoselection.
	int						iRumbleEffect;							// Which rumble effect to use when fired? (xbox)
	bool					bAutoSwitchTo;							// whether this weapon should be considered for autoswitching to
	bool					bAutoSwitchFrom;						// whether this weapon can be autoswitched away from when picking up another weapon or ammo
	int						iFlags;									// miscellaneous weapon flags
	char					szAmmo1[MAX_WEAPON_AMMO_NAME];			// "primary" ammo type
	char					szAmmo2[MAX_WEAPON_AMMO_NAME];			// "secondary" ammo type

	// Sound blocks
	char					aShootSounds[NUM_SHOOT_SOUND_TYPES][MAX_WEAPON_STRING];	

	int						iAmmoType;
	int						iAmmo2Type;
	bool					m_bMeleeWeapon;		// Melee weapons can always "fire" regardless of ammo.

	// This tells if the weapon was built right-handed (defaults to true).
	// This helps cl_righthand make the decision about whether to flip the model or not.
	bool					m_bBuiltRightHanded;
	bool					m_bAllowFlipping;	// False to disallow flipping the model, regardless of whether
												// it is built left or right handed.

// CLIENT DLL
	// Sprite data, read from the data file
	int						iSpriteCount;
	CHudTexture						*iconActive;
	CHudTexture	 					*iconInactive;
	CHudTexture 					*iconAmmo;
	CHudTexture 					*iconAmmo2;
	CHudTexture 					*iconCrosshair;
	CHudTexture 					*iconAutoaim;
	CHudTexture 					*iconZoomedCrosshair;
	CHudTexture 					*iconZoomedAutoaim;
	CHudTexture						*iconSmall;

// TF2 specific
	bool					bShowUsageHint;							// if true, then when you receive the weapon, show a hint about it

// SERVER DLL
};

enum
{
	TF_PROJECTILE_NONE,
	TF_PROJECTILE_BULLET,
	TF_PROJECTILE_ROCKET,
	TF_PROJECTILE_PIPEBOMB,
	TF_PROJECTILE_PIPEBOMB_REMOTE,
	TF_PROJECTILE_SYRINGE,
	TF_PROJECTILE_FLARE,
	TF_PROJECTILE_JAR,
	TF_PROJECTILE_ARROW,
	TF_PROJECTILE_FLAME_ROCKET,
	TF_PROJECTILE_JAR_MILK,
	TF_PROJECTILE_HEALING_BOLT,
	TF_PROJECTILE_ENERGY_BALL,
	TF_PROJECTILE_ENERGY_RING,
	TF_PROJECTILE_PIPEBOMB_PRACTICE,
	TF_PROJECTILE_CLEAVER,
	TF_PROJECTILE_STICKY_BALL,
	TF_PROJECTILE_CANNONBALL,
	TF_PROJECTILE_BUILDING_REPAIR_BOLT,
	TF_PROJECTILE_FESTIVE_ARROW,
	TF_PROJECTILE_THROWABLE,
	TF_PROJECTILE_SPELL,
	TF_PROJECTILE_FESTIVE_JAR,
	TF_PROJECTILE_FESTIVE_HEALING_BOLT,
	TF_PROJECTILE_BREADMONSTER_JARATE,
	TF_PROJECTILE_BREADMONSTER_MADMILK,
	TF_PROJECTILE_GRAPPLINGHOOK,
	TF_PROJECTILE_SENTRY_ROCKET,
	TF_PROJECTILE_BREAD_MONSTER,
	TF_NUM_PROJECTILES
};

#define TF_WEAPON_PRIMARY_MODE 0
#define TF_WEAPON_SECONDARY_MODE 1

struct WeaponData_t final
{
	int		m_nDamage;
	int		m_nBulletsPerShot;
	float	m_flRange;
	float	m_flSpread;
	float	m_flPunchAngle;
	float	m_flTimeFireDelay;				// Time to delay between firing
	float	m_flTimeIdle;					// Time to idle after firing
	float	m_flTimeIdleEmpty;				// Time to idle after firing last bullet in clip
	float	m_flTimeReloadStart;			// Time to start into a reload (ie. shotgun)
	float	m_flTimeReload;					// Time to reload
	bool	m_bDrawCrosshair;				// Should the weapon draw a crosshair
	int		m_iProjectile;					// The type of projectile this mode fires
	int		m_iAmmoPerShot;					// How much ammo each shot consumes
	float	m_flProjectileSpeed;			// Start speed for projectiles (nail, etc.); NOTE: union with something non-projectile
	float	m_flSmackDelay;					// how long after swing should damage happen for melee weapons
	bool	m_bUseRapidFireCrits;

	~WeaponData_t() = default;

	void Init( void )
	{
		m_nDamage = 0;
		m_nBulletsPerShot = 0;
		m_flRange = 0.0f;
		m_flSpread = 0.0f;
		m_flPunchAngle = 0.0f;
		m_flTimeFireDelay = 0.0f;
		m_flTimeIdle = 0.0f;
		m_flTimeIdleEmpty = 0.0f;
		m_flTimeReloadStart = 0.0f;
		m_flTimeReload = 0.0f;
		m_iProjectile = TF_PROJECTILE_NONE;
		m_iAmmoPerShot = 0;
		m_flProjectileSpeed = 0.0f;
		m_flSmackDelay = 0.0f;
		m_bUseRapidFireCrits = false;
	}
};

enum
{
	TF_WPN_TYPE_PRIMARY = 0,
	TF_WPN_TYPE_SECONDARY,
	TF_WPN_TYPE_MELEE,
	TF_WPN_TYPE_GRENADE,
	TF_WPN_TYPE_BUILDING,
	TF_WPN_TYPE_PDA,
	TF_WPN_TYPE_ITEM1,
	TF_WPN_TYPE_ITEM2,
	TF_WPN_TYPE_HEAD,
	TF_WPN_TYPE_MISC,
	TF_WPN_TYPE_MELEE_ALLCLASS,
	TF_WPN_TYPE_SECONDARY2,
	TF_WPN_TYPE_PRIMARY2,
	TF_WPN_TYPE_COUNT
};

class CTFWeaponInfo : public FileWeaponInfo_t
{
public:
	virtual void Parse( KeyValues *pKeyValuesData, const char *szWeaponName ) override
	{
		call_mfunc<void, CTFWeaponInfo, KeyValues *, const char *>(this, CTFWeaponInfoParseAddr, pKeyValuesData, szWeaponName);

		m_WeaponData[TF_WEAPON_SECONDARY_MODE].m_flProjectileSpeed = pKeyValuesData->GetFloat("Secondary_ProjectileSpeed", 0.0f);
	}

	CTFWeaponInfo()
		: FileWeaponInfo_t{}
	{
		m_WeaponData[0].Init();
		m_WeaponData[1].Init();

		m_bGrenade = false;
		m_flDamageRadius = 0.0f;
		m_flPrimerTime = 0.0f;
		m_bSuppressGrenTimer = false;
		m_bLowerWeapon = false;

		m_bHasTeamSkins_Viewmodel = false;
		m_bHasTeamSkins_Worldmodel = false;

		m_szMuzzleFlashModel[0] = '\0';
		m_flMuzzleFlashModelDuration = 0;
		m_szMuzzleFlashParticleEffect[0] = '\0';

		m_szTracerEffect[0] = '\0';

		m_szBrassModel[0] = '\0';
		m_bDoInstantEjectBrass = true;

		m_szExplosionSound[0] = '\0';
		m_szExplosionEffect[0] = '\0';
		m_szExplosionPlayerEffect[0] = '\0';
		m_szExplosionWaterEffect[0] = '\0';

		m_iWeaponType = TF_WPN_TYPE_PRIMARY;
	}

	~CTFWeaponInfo() = default;

	WeaponData_t	m_WeaponData[2];

	int		m_iWeaponType;
	
	// Grenade.
	bool	m_bGrenade;
	float	m_flDamageRadius;
	float	m_flPrimerTime;
	bool	m_bLowerWeapon;
	bool	m_bSuppressGrenTimer;

	// Skins
	bool	m_bHasTeamSkins_Viewmodel;
	bool	m_bHasTeamSkins_Worldmodel;

	// Muzzle flash
	char	m_szMuzzleFlashModel[128];
	float	m_flMuzzleFlashModelDuration;
	char	m_szMuzzleFlashParticleEffect[128];

	// Tracer
	char	m_szTracerEffect[128];

	// Eject Brass
	bool	m_bDoInstantEjectBrass;
	char	m_szBrassModel[128];

	// Explosion Effect
	char	m_szExplosionSound[128];
	char	m_szExplosionEffect[128];
	char	m_szExplosionPlayerEffect[128];
	char	m_szExplosionWaterEffect[128];

	bool	m_bDontDrop;
};

#define GAME_WEP_INFO CTFWeaponInfo

class custom_weapon_info final : public GAME_WEP_INFO
{
public:
	using base_class = GAME_WEP_INFO;

	custom_weapon_info()
		: base_class{}
	{
	}

	~custom_weapon_info() = default;

	struct custom_weapon_data
	{
		float m_flProjectileGravity = 0.0;
		float m_flProjectileSpread = 0.0;
	};

	custom_weapon_data m_WeaponDataCustom[2];

	virtual void Parse(KeyValues *pKeyValuesData, const char *szWeaponName) override final
	{
		base_class::Parse(pKeyValuesData, szWeaponName);

		m_WeaponDataCustom[TF_WEAPON_PRIMARY_MODE].m_flProjectileGravity = pKeyValuesData->GetFloat("ProjectileGravity", 0.0f);
		m_WeaponDataCustom[TF_WEAPON_PRIMARY_MODE].m_flProjectileSpread = pKeyValuesData->GetFloat("ProjectileSpread", 0.0f);

		m_WeaponDataCustom[TF_WEAPON_SECONDARY_MODE].m_flProjectileGravity = pKeyValuesData->GetFloat("Secondary_ProjectileGravity", 0.0f);
		m_WeaponDataCustom[TF_WEAPON_SECONDARY_MODE].m_flProjectileSpread = pKeyValuesData->GetFloat("Secondary_ProjectileSpread", 0.0f);
	}
};

DETOUR_DECL_MEMBER0(CTFWeaponBaseGunGetProjectileSpeed, float)
{
	CTFWeaponInfo *m_pWeaponInfo = *reinterpret_cast<CTFWeaponInfo **>(reinterpret_cast<unsigned char *>(this) + CTFWeaponBase_m_pWeaponInfo_offset);
	if(!m_pWeaponInfo) {
		return 0.0;
	}

	int m_iWeaponMode = *reinterpret_cast<int *>(reinterpret_cast<unsigned char *>(this) + CTFWeaponBase_m_iWeaponMode_offset);
	return m_pWeaponInfo->m_WeaponData[m_iWeaponMode].m_flProjectileSpeed;
}

DETOUR_DECL_MEMBER0(CTFWeaponBaseGunGetProjectileGravity, float)
{
	CTFWeaponInfo *m_pWeaponInfo = *reinterpret_cast<CTFWeaponInfo **>(reinterpret_cast<unsigned char *>(this) + CTFWeaponBase_m_pWeaponInfo_offset);
	if(!m_pWeaponInfo) {
		return 0.0;
	}

	int m_iWeaponMode = *reinterpret_cast<int *>(reinterpret_cast<unsigned char *>(this) + CTFWeaponBase_m_iWeaponMode_offset);
	return reinterpret_cast<custom_weapon_info *>(m_pWeaponInfo)->m_WeaponDataCustom[m_iWeaponMode].m_flProjectileGravity;
}

DETOUR_DECL_MEMBER0(CTFWeaponBaseGunGetProjectileSpread, float)
{
	CTFWeaponInfo *m_pWeaponInfo = *reinterpret_cast<CTFWeaponInfo **>(reinterpret_cast<unsigned char *>(this) + CTFWeaponBase_m_pWeaponInfo_offset);
	if(!m_pWeaponInfo) {
		return 0.0;
	}

	int m_iWeaponMode = *reinterpret_cast<int *>(reinterpret_cast<unsigned char *>(this) + CTFWeaponBase_m_iWeaponMode_offset);
	return reinterpret_cast<custom_weapon_info *>(m_pWeaponInfo)->m_WeaponDataCustom[m_iWeaponMode].m_flProjectileSpread;
}

DETOUR_DECL_STATIC0(CreateWeaponInfo, FileWeaponInfo_t *)
{
	return new custom_weapon_info{};
}

static CUtlDict< FileWeaponInfo_t*, unsigned short > *WeaponInfoDatabase()
{
	return reinterpret_cast<CUtlDict< FileWeaponInfo_t*, unsigned short > *>(m_WeaponInfoDatabaseAddr);
}

static void remove_weapon_info(const char *name, unsigned short idx)
{
	delete reinterpret_cast<custom_weapon_info *>((*WeaponInfoDatabase())[idx]);
	WeaponInfoDatabase()->RemoveAt(idx);
}

HandleType_t ammo_handle = 0;

cell_t uncache_weapon_file(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);

	char fileBase[512];
	Q_FileBase( name, fileBase, sizeof(fileBase) );

	unsigned short idx = WeaponInfoDatabase()->Find(fileBase);
	if(idx == WeaponInfoDatabase()->InvalidIndex()) {
		return 0;
	}

	remove_weapon_info(fileBase, idx);

	return 1;
}

static WEAPON_FILE_INFO_HANDLE FindWeaponInfoSlot( const char *name )
{
	// Complain about duplicately defined metaclass names...
	unsigned short lookup = WeaponInfoDatabase()->Find( name );
	if ( lookup != WeaponInfoDatabase()->InvalidIndex() )
	{
		return lookup;
	}

	FileWeaponInfo_t *insert = CreateWeaponInfo();

	lookup = WeaponInfoDatabase()->Insert( name, insert );
	return lookup;
}

FileWeaponInfo_t *GetFileWeaponInfoFromHandle( WEAPON_FILE_INFO_HANDLE handle )
{
	return (void_to_func<FileWeaponInfo_t *(*)(WEAPON_FILE_INFO_HANDLE)>(GetFileWeaponInfoFromHandleAddr))(handle);
}

IForward *get_weapon_script = nullptr;

static char last_script[PATH_MAX];
CBaseEntity *last_weapon = nullptr;

static bool force_parse = false;

bool ReadWeaponDataFromFileForSlotKV( KeyValues *pKV, const char *szWeaponName, WEAPON_FILE_INFO_HANDLE *phandle )
{
	if(get_weapon_script->GetFunctionCount() > 0) {
		get_weapon_script->PushCell(last_weapon ? gamehelpers->EntityToBCompatRef(last_weapon) : -1);
		get_weapon_script->PushStringEx((char *)szWeaponName, strlen(szWeaponName)+1, SM_PARAM_STRING_COPY|SM_PARAM_STRING_UTF8, 0);
		get_weapon_script->PushStringEx(last_script, sizeof(last_script), SM_PARAM_STRING_UTF8, SM_PARAM_COPYBACK);
		get_weapon_script->PushCell(sizeof(last_script));
		cell_t res = 0;
		get_weapon_script->Execute(&res);

		switch(res) {
			case Pl_Changed: {
				szWeaponName = last_script;
			}
			case Pl_Handled:
			case Pl_Stop: {
				return false;
			}
		}
	}

	if ( !phandle )
	{
		return false;
	}
	
	*phandle = FindWeaponInfoSlot( szWeaponName );
	FileWeaponInfo_t *pFileInfo = GetFileWeaponInfoFromHandle( *phandle );

	if ( pFileInfo->bParsedScript ) {
		if(!force_parse) {
			return true;
		}

		pFileInfo->bParsedScript = false;
		new (&pFileInfo) GAME_WEP_INFO{};
	}

	char sz[128];
	Q_snprintf( sz, sizeof( sz ), "scripts/%s", szWeaponName );

	if ( !pKV )
		return false;

	pFileInfo->Parse( pKV, szWeaponName );

	return true;
}

DETOUR_DECL_STATIC4(ReadWeaponDataFromFileForSlot, bool, IFileSystem*, pFilesystem, const char *, szWeaponName, WEAPON_FILE_INFO_HANDLE *, phandle, const unsigned char *, pICEKey)
{
	if(get_weapon_script->GetFunctionCount() > 0) {
		get_weapon_script->PushCell(last_weapon ? gamehelpers->EntityToBCompatRef(last_weapon) : -1);
		get_weapon_script->PushStringEx((char *)szWeaponName, strlen(szWeaponName)+1, SM_PARAM_STRING_COPY|SM_PARAM_STRING_UTF8, 0);
		get_weapon_script->PushStringEx(last_script, sizeof(last_script), SM_PARAM_STRING_UTF8, SM_PARAM_COPYBACK);
		get_weapon_script->PushCell(sizeof(last_script));
		cell_t res = 0;
		get_weapon_script->Execute(&res);

		switch(res) {
			case Pl_Changed: {
				szWeaponName = last_script;
			}
			case Pl_Handled:
			case Pl_Stop: {
				return false;
			}
		}
	}

	if ( !phandle )
	{
		return false;
	}

	*phandle = FindWeaponInfoSlot( szWeaponName );
	FileWeaponInfo_t *pFileInfo = GetFileWeaponInfoFromHandle( *phandle );

	if ( pFileInfo->bParsedScript ) {
		if(!force_parse) {
			return true;
		}

		pFileInfo->bParsedScript = false;
		new (&pFileInfo) GAME_WEP_INFO{};
	}

	return DETOUR_STATIC_CALL(ReadWeaponDataFromFileForSlot)(pFilesystem, szWeaponName, phandle, pICEKey);
}

cell_t precache_weapon_file(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);

	char fileBase[512];
	Q_FileBase( name, fileBase, sizeof(fileBase) );

	bool force = (params[2] != 0);

	if(force) {
		unsigned short idx = WeaponInfoDatabase()->Find(fileBase);
		if(idx != WeaponInfoDatabase()->InvalidIndex()) {
			remove_weapon_info(fileBase, idx);
		}
	}

	if(!g_pGameRules) {
		return pContext->ThrowNativeError("gamerules is not loaded");
	}

	const unsigned char *pICEKey = call_vfunc<const unsigned char *>(g_pGameRules, GetEncryptionKeyoffset);

	WEAPON_FILE_INFO_HANDLE tmp;
	force_parse = force;
	bool added = ReadWeaponDataFromFileForSlot(filesystem, fileBase, &tmp, pICEKey);
	force_parse = false;
	return added;
}

cell_t precache_weapon_kv(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[2], &name);

	char fileBase[512];
	Q_FileBase( name, fileBase, sizeof(fileBase) );

	bool force = (params[3] != 0);

	if(force) {
		unsigned short idx = WeaponInfoDatabase()->Find(fileBase);
		if(idx != WeaponInfoDatabase()->InvalidIndex()) {
			remove_weapon_info(fileBase, idx);
		}
	}

	HandleError err;
	KeyValues *pKV = smutils->ReadKeyValuesHandle(params[1], &err, false);
	if(!pKV) {
		return pContext->ThrowNativeError("Invalid KeyValues handle %x (error: %d)", params[1], err);
	}

	WEAPON_FILE_INFO_HANDLE tmp;
	force_parse = force;
	bool added = ReadWeaponDataFromFileForSlotKV(pKV, fileBase, &tmp);
	force_parse = false;
	return added;
}

cell_t get_ammo_index(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);

	return (void_to_func<CAmmoDef *(*)()>(GetAmmoDefAddr))()->Index(name);
}

struct ammo_handle_obj
{
	ammo_handle_obj(const char *name_, int idx, IdentityToken_t *ident_);
	~ammo_handle_obj();

	std::string name;
	int index;
	IdentityToken_t *ident;
};

static std::unordered_map<std::string, ammo_handle_obj *> ammos;

ammo_handle_obj::ammo_handle_obj(const char *name_, int idx, IdentityToken_t *ident_)
	: name{name_}, index{idx}, ident{ident_}
{
	ammos[name] = this;
}

ammo_handle_obj::~ammo_handle_obj()
{
	ammos.erase(name);
}

cell_t add_ammo_type(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);

	CAmmoDef *def = (void_to_func<CAmmoDef *(*)()>(GetAmmoDefAddr))();

	if(def->Index(name) != -1) {
		return pContext->ThrowNativeError("ammo %s already registered", name);
	}

	if(!def->AddAmmoType(name, params[2], params[3], params[4], params[5], params[6], sp_ctof(params[7]), params[8], params[9], params[10])) {
		return BAD_HANDLE;
	}

	ammo_handle_obj *obj = new ammo_handle_obj{name, def->m_nAmmoIndex-1, pContext->GetIdentity()};
	return handlesys->CreateHandle(ammo_handle, obj, pContext->GetIdentity(), myself->GetIdentity(), nullptr);
}

cell_t add_ammo_type_cvar(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);

	CAmmoDef *def = (void_to_func<CAmmoDef *(*)()>(GetAmmoDefAddr))();

	if(def->Index(name) != -1) {
		return pContext->ThrowNativeError("ammo %s already registered", name);
	}

	char *plr_cvar = nullptr;
	pContext->LocalToStringNULL(params[4], &plr_cvar);

	char *npc_cvar = nullptr;
	pContext->LocalToStringNULL(params[5], &npc_cvar);

	char *carry_cvar = nullptr;
	pContext->LocalToStringNULL(params[6], &carry_cvar);

	if(!def->AddAmmoType(name, params[2], params[3], plr_cvar, npc_cvar, carry_cvar, sp_ctof(params[7]), params[8], params[9], params[10])) {
		return BAD_HANDLE;
	}

	ammo_handle_obj *obj = new ammo_handle_obj{name, def->m_nAmmoIndex-1, pContext->GetIdentity()};
	return handlesys->CreateHandle(ammo_handle, obj, pContext->GetIdentity(), myself->GetIdentity(), nullptr);
}

cell_t get_max_ammo_count(IPluginContext *pContext, const cell_t *params)
{
	return MAX_AMMO_TYPES;
}

cell_t get_ammo_count(IPluginContext *pContext, const cell_t *params)
{
	CAmmoDef *def = (void_to_func<CAmmoDef *(*)()>(GetAmmoDefAddr))();

	return def->m_nAmmoIndex;
}

cell_t weapon_info_count(IPluginContext *pContext, const cell_t *params)
{
	return WeaponInfoDatabase()->Count();
}

int CBaseEntityFireBullets = -1;

struct FireBulletsInfo_t;

class CBaseEntity : public IServerEntity
{
public:
	void FireBullets( const FireBulletsInfo_t &info )
	{
		call_vfunc<void, CBaseEntity, const FireBulletsInfo_t &>(this, CBaseEntityFireBullets, info);
	}
};

class CTFWeaponBase : public CBaseEntity
{
};

class CTFDroppedWeapon : public CBaseEntity
{
};

SH_DECL_MANUALHOOK0_void(GenericDtor, 1, 0, 0)

#if 0
struct callback_holder_t
{
	struct callback_t
	{
		IPluginFunction *callback{nullptr};
		cell_t data{0};
	};

	void add_hook(CBaseEntity *pEntity, std::string &&name, IPluginFunction *func, cell_t data)
	{
		auto it{callbacks.find(name)};
		if(it == callbacks.end()) {
			it = callbacks.emplace(std::pair<std::string, callback_t>{std::move(name), callback_t{}}).first;
		}

		if(!it->second.callback) {
			
		}

		it->second.callback = func;
		it->second.data = data;
	}

	std::unordered_map<std::string, callback_t> callbacks{};
};

static std::unordered_map<int, callback_holder_t *> callbacks{};

cell_t set_weapon_function(IPluginContext *pContext, const cell_t *params)
{
	int idx{gamehelpers->ReferenceToBCompatRef(params[1])};
	CBaseEntity *pEntity{gamehelpers->ReferenceToEntity(params[1])};
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", idx);
	}

	char *name_ptr;
	pContext->LocalToString(params[2], &name_ptr);
	std::string name{name_ptr};

	IPluginFunction *callback{pContext->GetFunctionById(params[3])};

	auto holder_it{callbacks.find(idx)};
	if(holder_it == callbacks.end()) {
		holder_it = callbacks.emplace(std::pair<int, callback_holder_t *>{idx, new callback_holder_t{}}).first;
	}

	holder_it->second->add_hook(pEntity, std::move(name), callback, params[4]);

	return 0;
}
#endif

void Sample::OnEntityDestroyed(CBaseEntity *pEntity) noexcept
{
#if 0
	if(!pEntity) {
		return;
	}

	const int idx{gamehelpers->EntityToBCompatRef(pEntity)};

	auto it{callbacks.find(idx)};
	if(it != callbacks.end()) {
		delete it->second;
		callbacks.erase(it);
	}
#endif
}

void Sample::OnPluginUnloaded(IPlugin *plugin) noexcept
{
#if 0
	auto it{callbacks.begin()};
	while(it != callbacks.end()) {
		auto func_it{it->second->callbacks.begin()};
		while(func_it != it->second->callbacks.end()) {
			if(func_it->second.callback->GetParentContext() == plugin->GetBaseContext()) {
				it->second->callbacks.erase(func_it);
				continue;
			}
			++func_it;
		}
		if(it->second->callbacks.empty()) {
			delete it->second;
			callbacks.erase(it);
			continue;
		}
		++it;
	}
#endif
}

Vector addr_deref_vec(const cell_t *&addr)
{
	Vector ret;

	ret.x = sp_ctof(*addr);
	++addr;

	ret.y = sp_ctof(*addr);
	++addr;

	ret.z = sp_ctof(*addr);
	++addr;

	return ret;
}

void AddrToBulletInfo(const cell_t *addr, FireBulletsInfo_t &info)
{
	info.m_iShots = *addr;
	++addr;
	info.m_vecSrc = addr_deref_vec(addr);
	info.m_vecDirShooting = addr_deref_vec(addr);
	info.m_vecSpread = addr_deref_vec(addr);
	info.m_flDistance = sp_ctof(*addr);
	++addr;
	info.m_iAmmoType = *addr;
	++addr;
	info.m_iTracerFreq = *addr;
	++addr;
	info.m_flDamage = sp_ctof(*addr);
	++addr;
#if SOURCE_ENGINE == SE_TF2
	info.m_iPlayerDamage = *addr;
#elif SOURCE_ENGINE == SE_LEFT4DEAD2
	info.m_flPlayerDamage = sp_ctof(*addr);
#endif
	++addr;
	info.m_nFlags = *addr;
	++addr;
	info.m_flDamageForceScale = sp_ctof(*addr);
	++addr;
	info.m_pAttacker = gamehelpers->ReferenceToEntity(*addr);
	++addr;
	info.m_pAdditionalIgnoreEnt = gamehelpers->ReferenceToEntity(*addr);
	++addr;
	info.m_bPrimaryAttack = *addr;
	++addr;
#if SOURCE_ENGINE == SE_TF2
	info.m_bUseServerRandomSeed = *addr;
	++addr;
#endif
}

static cell_t BaseEntityFireBullets(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = (CBaseEntity *)gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	FireBulletsInfo_t info{};
	
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[2], &addr);

	::AddrToBulletInfo(addr, info);

	pEntity->FireBullets(info);
	return 0;
}

sp_nativeinfo_t natives[] =
{
	//{"set_weapon_function", set_weapon_function},
	{"weapon_info_count", weapon_info_count},
	{"uncache_weapon_file", uncache_weapon_file},
	{"precache_weapon_file", precache_weapon_file},
	{"precache_weapon_kv", precache_weapon_kv},
	{"get_max_ammo_count", get_max_ammo_count},
	{"get_ammo_count", get_ammo_count},
	{"get_ammo_index", get_ammo_index},
	{"add_ammo_type", add_ammo_type},
	{"add_ammo_type_cvar", add_ammo_type_cvar},
	{"FireBullets", BaseEntityFireBullets},
	{NULL, NULL}
};

CBaseEntity *last_client = nullptr;

IForward *translate_weapon_classname = nullptr;

static char last_classname[64];
DETOUR_DECL_STATIC2(TranslateWeaponEntForClass, const char *, const char *, pszName, int, iClass)
{
	if(translate_weapon_classname->GetFunctionCount() > 0) {
		translate_weapon_classname->PushCell(last_client ? gamehelpers->EntityToBCompatRef(last_client) : -1);
		translate_weapon_classname->PushCell(iClass);
		translate_weapon_classname->PushCell(last_weapon ? gamehelpers->EntityToBCompatRef(last_weapon) : -1);
		translate_weapon_classname->PushStringEx((char *)pszName, strlen(pszName)+1, SM_PARAM_STRING_COPY|SM_PARAM_STRING_UTF8, 0);
		translate_weapon_classname->PushStringEx(last_classname, sizeof(last_classname), SM_PARAM_STRING_UTF8, SM_PARAM_COPYBACK);
		translate_weapon_classname->PushCell(sizeof(last_classname));
		cell_t res = 0;
		translate_weapon_classname->Execute(&res);

		switch(res) {
			case Pl_Changed: {
				return last_classname;
			}
			case Pl_Handled:
			case Pl_Stop: {
				return pszName;
			}
		}
	}

	return DETOUR_STATIC_CALL(TranslateWeaponEntForClass)(pszName, iClass);
}

DETOUR_DECL_MEMBER0(CBaseCombatWeaponPrecache, void)
{
	last_weapon = (CBaseEntity *)this;
	DETOUR_MEMBER_CALL(CBaseCombatWeaponPrecache)();
	last_weapon = nullptr;
}

struct TFPlayerClassData_t;
class CEconItemView;
DETOUR_DECL_MEMBER4(CTFPlayerItemsMatch, bool, TFPlayerClassData_t *, pData, CEconItemView *, pCurWeaponItem, CEconItemView *, pNewWeaponItem, CTFWeaponBase *, pWpnEntity)
{
	last_client = (CBaseEntity *)this;
	last_weapon = pWpnEntity;
	bool ret = DETOUR_MEMBER_CALL(CTFPlayerItemsMatch)(pData, pCurWeaponItem, pNewWeaponItem, pWpnEntity);
	last_client = nullptr;
	last_weapon = nullptr;
	return ret;
}

DETOUR_DECL_MEMBER4(CTFPlayerGiveNamedItem, CBaseEntity *, const char *, pszName, int, iSubType, const CEconItemView *, pScriptItem, bool, bForce)
{
	last_client = (CBaseEntity *)this;
	CBaseEntity *ret = DETOUR_MEMBER_CALL(CTFPlayerGiveNamedItem)(pszName, iSubType, pScriptItem, bForce);
	last_client = nullptr;
	return ret;
}

DETOUR_DECL_MEMBER1(CTFPlayerPickupWeaponFromOther, bool, CTFDroppedWeapon *, pDroppedWeapon)
{
	last_client = (CBaseEntity *)this;
	last_weapon = pDroppedWeapon;
	bool ret = DETOUR_MEMBER_CALL(CTFPlayerPickupWeaponFromOther)(pDroppedWeapon);
	last_client = nullptr;
	last_weapon = nullptr;
	return ret;
}

IGameConfig *g_pGameConf = nullptr;

CDetour *TranslateWeaponEntForClassDetour = nullptr;
CDetour *ReadWeaponDataFromFileForSlotDetour = nullptr;
CDetour *CBaseCombatWeaponPrecacheDetour = nullptr;
CDetour *CTFPlayerItemsMatchDetour = nullptr;
CDetour *CTFPlayerGiveNamedItemDetour = nullptr;
CDetour *CTFPlayerPickupWeaponFromOtherDetour = nullptr;

CDetour *CreateWeaponInfoDetour = nullptr;
CDetour *CTFWeaponBaseGunGetProjectileSpeedDetour = nullptr;
CDetour *CTFWeaponBaseGunGetProjectileGravityDetour = nullptr;
CDetour *CTFWeaponBaseGunGetProjectileSpreadDetour = nullptr;

int CGameRulesGetSkillLevel = -1;

class CGameRules
{
public:
	int GetSkillLevel()
	{
		return call_vfunc<int>(this, CGameRulesGetSkillLevel);
	}
};

static bool gamerules_vtable_assigned{false};

// Quantity scale for ammo received by the player.
ConVar	sk_ammo_qty_scale1 ( "sk_ammo_qty_scale1", "1.20", FCVAR_REPLICATED );
ConVar	sk_ammo_qty_scale2 ( "sk_ammo_qty_scale2", "1.00", FCVAR_REPLICATED );
ConVar	sk_ammo_qty_scale3 ( "sk_ammo_qty_scale3", "0.60", FCVAR_REPLICATED );

class GameRulesVTableHack
{
public:
	float GetAmmoQuantityScale( int iAmmoIndex )
	{
		CGameRules *pThis = (CGameRules *)this;

		switch( pThis->GetSkillLevel() )
		{
		case SKILL_EASY:
			return sk_ammo_qty_scale1.GetFloat();

		case SKILL_MEDIUM:
			return sk_ammo_qty_scale2.GetFloat();

		case SKILL_HARD:
			return sk_ammo_qty_scale3.GetFloat();

		default:
			return 0.0f;
		}
	}
};

#include <sourcehook/sh_memory.h>

int CGameRulesGetAmmoQuantityScale = -1;

void Sample::OnCoreMapStart(edict_t * pEdictList, int edictCount, int clientMax)
{
	if(!gamerules_vtable_assigned) {
		CGameRules *gamerules{(CGameRules *)g_pSDKTools->GetGameRules()};
		if(gamerules) {
			void **vtabl = *(void ***)gamerules;

			SourceHook::SetMemAccess(vtabl, (CGameRulesGetAmmoQuantityScale * sizeof(void *)) + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);

			vtabl[CGameRulesGetAmmoQuantityScale] = func_to_void(&GameRulesVTableHack::GetAmmoQuantityScale);

			gamerules_vtable_assigned = true;
		}
	}
}

enum ETFAmmoType
{
	TF_AMMO_DUMMY = 0,	// Dummy index to make the CAmmoDef indices correct for the other ammo types.
	TF_AMMO_PRIMARY,
	TF_AMMO_SECONDARY,
	TF_AMMO_METAL,
	TF_AMMO_GRENADES1,
	TF_AMMO_GRENADES2,
	TF_AMMO_GRENADES3,	// Utility Slot Grenades
	TF_AMMO_COUNT,

	//
	// ADD NEW ITEMS HERE TO AVOID BREAKING DEMOS
	//
};

struct ammo_dmg_cvar_t
{
	ConVar *plr_dmg;
	ConVar *npc_dmg;

	~ammo_dmg_cvar_t()
	{
		delete plr_dmg;
		delete npc_dmg;
	}
};

static ammo_dmg_cvar_t tf_ammo_dmg_cvars[TF_AMMO_COUNT]{
	{new ConVar{"sk_plr_dmg_dummy", "0"}, new ConVar{"sk_npc_dmg_dummy", "0"}},
	{new ConVar{"sk_plr_dmg_primary", "6"}, new ConVar{"sk_npc_dmg_primary", "6"}},
	{new ConVar{"sk_plr_dmg_secondary", "15"}, new ConVar{"sk_npc_dmg_secondary", "15"}},
	{new ConVar{"sk_plr_dmg_metal", "0"}, new ConVar{"sk_npc_dmg_metal", "0"}},
	{new ConVar{"sk_plr_dmg_grenade1", "120"}, new ConVar{"sk_npc_dmg_grenade1", "120"}},
	{new ConVar{"sk_plr_dmg_grenade2", "120"}, new ConVar{"sk_npc_dmg_grenade2", "120"}},
	{new ConVar{"sk_plr_dmg_grenade3", "120"}, new ConVar{"sk_npc_dmg_grenade3", "120"}},
};

bool Sample::SDK_OnLoad(char *error, size_t maxlen, bool late)
{
	gameconfs->LoadGameConfigFile("wpnhack", &g_pGameConf, nullptr, 0);

	CDetourManager::Init(g_pSM->GetScriptingEngine(), g_pGameConf);

	g_pGameConf->GetOffset("CGameRules::GetEncryptionKey", &GetEncryptionKeyoffset);
	g_pGameConf->GetMemSig("GetFileWeaponInfoFromHandle", &GetFileWeaponInfoFromHandleAddr);
	g_pGameConf->GetMemSig("m_WeaponInfoDatabase", &m_WeaponInfoDatabaseAddr);

	g_pGameConf->GetMemSig("FileWeaponInfo_t::Parse", &FileWeaponInfo_tParseAddr);
	g_pGameConf->GetMemSig("CTFWeaponInfo::Parse", &CTFWeaponInfoParseAddr);

	g_pGameConf->GetMemSig("GetAmmoDef", &GetAmmoDefAddr);

	g_pGameConf->GetOffset("CGameRules::GetAmmoQuantityScale", &CGameRulesGetAmmoQuantityScale);
	g_pGameConf->GetOffset("CGameRules::GetSkillLevel", &CGameRulesGetSkillLevel);

	g_pGameConf->GetOffset("CBaseEntity::FireBullets", &CBaseEntityFireBullets);

	int tmp_off;

	TranslateWeaponEntForClassDetour = DETOUR_CREATE_STATIC(TranslateWeaponEntForClass, "TranslateWeaponEntForClass")
	TranslateWeaponEntForClassDetour->EnableDetour();

	ReadWeaponDataFromFileForSlotDetour = DETOUR_CREATE_STATIC(ReadWeaponDataFromFileForSlot, "ReadWeaponDataFromFileForSlot")
	ReadWeaponDataFromFileForSlotDetour->EnableDetour();

	CBaseCombatWeaponPrecacheDetour = DETOUR_CREATE_MEMBER(CBaseCombatWeaponPrecache, "CBaseCombatWeapon::Precache")
	CBaseCombatWeaponPrecacheDetour->EnableDetour();

	CTFPlayerItemsMatchDetour = DETOUR_CREATE_MEMBER(CTFPlayerItemsMatch, "CTFPlayer::ItemsMatch")
	CTFPlayerItemsMatchDetour->EnableDetour();

	CTFPlayerGiveNamedItemDetour = DETOUR_CREATE_MEMBER(CTFPlayerGiveNamedItem, "CTFPlayer::GiveNamedItem")
	CTFPlayerGiveNamedItemDetour->EnableDetour();

	CTFPlayerPickupWeaponFromOtherDetour = DETOUR_CREATE_MEMBER(CTFPlayerPickupWeaponFromOther, "CTFPlayer::PickupWeaponFromOther")
	CTFPlayerPickupWeaponFromOtherDetour->EnableDetour();

	CreateWeaponInfoDetour = DETOUR_CREATE_STATIC(CreateWeaponInfo, "CreateWeaponInfo")
	CreateWeaponInfoDetour->EnableDetour();

	CTFWeaponBaseGunGetProjectileSpeedDetour = DETOUR_CREATE_MEMBER(CTFWeaponBaseGunGetProjectileSpeed, "CTFWeaponBaseGun::GetProjectileSpeed")
	CTFWeaponBaseGunGetProjectileSpeedDetour->EnableDetour();

	CTFWeaponBaseGunGetProjectileGravityDetour = DETOUR_CREATE_MEMBER(CTFWeaponBaseGunGetProjectileGravity, "CTFWeaponBaseGun::GetProjectileGravity")
	CTFWeaponBaseGunGetProjectileGravityDetour->EnableDetour();

	CTFWeaponBaseGunGetProjectileSpreadDetour = DETOUR_CREATE_MEMBER(CTFWeaponBaseGunGetProjectileSpread, "CTFWeaponBaseGun::GetProjectileSpread")
	CTFWeaponBaseGunGetProjectileSpreadDetour->EnableDetour();

	sm_sendprop_info_t tmp_sp_info;

	gamehelpers->FindSendPropInfo("CTFWeaponBase", "m_flReloadPriorNextFire", &tmp_sp_info);
	CTFWeaponBase_m_pWeaponInfo_offset = tmp_sp_info.actual_offset;
	g_pGameConf->GetOffset("CTFWeaponBase::m_pWeaponInfo", &tmp_off);
	CTFWeaponBase_m_pWeaponInfo_offset += tmp_off;

	gamehelpers->FindSendPropInfo("CTFWeaponBase", "m_iReloadMode", &tmp_sp_info);
	CTFWeaponBase_m_iWeaponMode_offset = tmp_sp_info.actual_offset;
	g_pGameConf->GetOffset("CTFWeaponBase::m_iWeaponMode", &tmp_off);
	CTFWeaponBase_m_iWeaponMode_offset -= tmp_off;

	translate_weapon_classname = forwards->CreateForward("translate_weapon_classname", ET_Event, 6, nullptr, Param_Cell, Param_Cell, Param_Cell, Param_String, Param_String, Param_Cell);
	get_weapon_script = forwards->CreateForward("get_weapon_script", ET_Event, 4, nullptr, Param_Cell, Param_String, Param_String, Param_Cell);

	ammo_handle = handlesys->CreateType("ammotype", this, 0, nullptr, nullptr, myself->GetIdentity(), nullptr);

	sharesys->AddInterface(myself, this);
	sharesys->RegisterLibrary(myself, "wpnhack");

	sharesys->AddNatives(myself, natives);

	CAmmoDef *def = (void_to_func<CAmmoDef *(*)()>(GetAmmoDefAddr))();

	for ( int i=1; i < TF_AMMO_COUNT; i++ ) {
		def->m_AmmoType[i].pPlrDmgCVar = tf_ammo_dmg_cvars[i].plr_dmg;
		def->m_AmmoType[i].pPlrDmg = USE_CVAR;
		def->m_AmmoType[i].pNPCDmgCVar = tf_ammo_dmg_cvars[i].npc_dmg;
		def->m_AmmoType[i].pNPCDmg = USE_CVAR;
	}

	return true;
}

void Sample::SDK_OnAllLoaded()
{
	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);
	SM_GET_LATE_IFACE(SDKHOOKS, g_pSDKHooks);

	g_pGameRules = (CGameRules *)g_pSDKTools->GetGameRules();

	g_pSDKHooks->AddEntityListener(this);
}

void Sample::SDK_OnUnload()
{
	forwards->ReleaseForward(translate_weapon_classname);
	forwards->ReleaseForward(get_weapon_script);

	TranslateWeaponEntForClassDetour->Destroy();
	ReadWeaponDataFromFileForSlotDetour->Destroy();
	CBaseCombatWeaponPrecacheDetour->Destroy();
	CTFPlayerItemsMatchDetour->Destroy();
	CTFPlayerGiveNamedItemDetour->Destroy();
	CTFPlayerPickupWeaponFromOtherDetour->Destroy();

	CreateWeaponInfoDetour->Destroy();
	CTFWeaponBaseGunGetProjectileSpeedDetour->Destroy();
	CTFWeaponBaseGunGetProjectileGravityDetour->Destroy();
	CTFWeaponBaseGunGetProjectileSpreadDetour->Destroy();

	plsys->RemovePluginsListener(this);
	g_pSDKHooks->RemoveEntityListener(this);

	gameconfs->CloseGameConfigFile(g_pGameConf);
}

bool Sample::RegisterConCommandBase(ConCommandBase *pCommand)
{
	META_REGCVAR(pCommand);
	return true;
}

bool Sample::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_CURRENT(GetEngineFactory, cvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, filesystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	g_pCVar = cvar;
	ConVar_Register(0, this);
	return true;
}

void Sample::OnHandleDestroy(HandleType_t type, void *object)
{
	if(type == ammo_handle) {
		ammo_handle_obj *obj = (ammo_handle_obj *)object;

		CAmmoDef *def = (void_to_func<CAmmoDef *(*)()>(GetAmmoDefAddr))();

		for(int i = obj->index; i < def->m_nAmmoIndex-1; ++i) {
			def->m_AmmoType[i] = std::move(def->m_AmmoType[i+1]);
		}

		--def->m_nAmmoIndex;

		delete obj;
	}
}