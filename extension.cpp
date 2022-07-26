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

#define GAME_DLL

#include <utility>
#include <unordered_map>
#include <string>

using namespace std::literals::string_literals;

#ifndef FMTFUNCTION
#define FMTFUNCTION(...)
#endif

#include "extension.h"
#include <tier1/utldict.h>
#include <ISDKTools.h>
#include <vstdlib/cvar.h>
#include <tier1/convar.h>
#include <filesystem.h>
#include <CDetour/detours.h>
#include <ISDKHooks.h>
#include <shareddefs.h>
#include <toolframework/itoolentity.h>
#include <ehandle.h>
using EHANDLE = CHandle<CBaseEntity>;
#include <util.h>
#include <ServerNetworkProperty.h>
#define DECLARE_PREDICTABLE()
#include <collisionproperty.h>
#include <takedamageinfo.h>

#define	SF_NORESPAWN	( 1 << 30 )

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

Sample g_Sample;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_Sample);

static ISDKHooks *g_pSDKHooks{nullptr};

CBaseEntityList *g_pEntityList = nullptr;
CGlobalVars *gpGlobals = nullptr;
IServerTools *servertools = nullptr;
IEntityFactoryDictionary *dictionary{nullptr};

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

template <typename T>
int vfunc_index(T func)
{
	SourceHook::MemFuncInfo info{};
	SourceHook::GetFuncInfo<T>(func, info);
	return info.vtblindex;
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

	float DamageForce(int nAmmoIndex)
	{
		if ( nAmmoIndex < 1 || nAmmoIndex >= m_nAmmoIndex )
			return 0;

		return m_AmmoType[nAmmoIndex].physicsForceImpulse;
	}
};

void *GetAmmoDefAddr;

CAmmoDef *GetAmmoDef()
{
	return (void_to_func<CAmmoDef *(*)()>(GetAmmoDefAddr))();
}

int Sample::PlrDamage(int nAmmoIndex)
{ return GetAmmoDef()->PlrDamage(nAmmoIndex); }
int Sample::NPCDamage(int nAmmoIndex)
{ return GetAmmoDef()->NPCDamage(nAmmoIndex); }
int Sample::DamageType(int nAmmoIndex)
{ return GetAmmoDef()->DamageType(nAmmoIndex); }
int Sample::Index(const char *name)
{ return GetAmmoDef()->Index(name); }
float Sample::DamageForce(int nAmmoIndex)
{ return GetAmmoDef()->DamageForce(nAmmoIndex); }

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

		const char *pszWeaponType = pKeyValuesData->GetString( "WeaponType" );
		if ( !Q_strcmp( pszWeaponType, "head" ) )
		{
			m_iWeaponType = TF_WPN_TYPE_HEAD;
		}
		else if ( !Q_strcmp( pszWeaponType, "misc" ) )
		{
			m_iWeaponType = TF_WPN_TYPE_MISC;
		}
		else if ( !Q_strcmp( pszWeaponType, "melee_allclass" ) )
		{
			m_iWeaponType = TF_WPN_TYPE_MELEE_ALLCLASS;
		}
		else if ( !Q_strcmp( pszWeaponType, "secondary2" ) )
		{
			m_iWeaponType = TF_WPN_TYPE_SECONDARY2;
		}
		else if ( !Q_strcmp( pszWeaponType, "primary2" ) )
		{
			m_iWeaponType = TF_WPN_TYPE_PRIMARY2;
		}
	}

	CTFWeaponInfo() = delete;

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

const char *g_szWeaponTypeSubstrings[] =
{
	// Weapons & Equipment
	"PRIMARY",
	"SECONDARY",
	"MELEE",
	"GRENADE",
	"BUILDING",
	"PDA",
	"ITEM1",
	"ITEM2",
	"HEAD",
	"MISC",
	"MELEE_ALLCLASS",
	"SECONDARY2",
	"PRIMARY2"
};
COMPILE_TIME_ASSERT( ARRAYSIZE( g_szWeaponTypeSubstrings ) == TF_WPN_TYPE_COUNT );

int StringFieldToInt( const char *szValue, const char **pValueStrings, int iNumStrings ) 
{
	if ( !szValue || !szValue[0] )
		return -1;

	for ( int i = 0; i < iNumStrings; i++ )
	{
		if ( !Q_stricmp(szValue, pValueStrings[i]) )
			return i;
	}

	return -1;
}

class custom_weapon_info final : public GAME_WEP_INFO
{
public:
	using base_class = GAME_WEP_INFO;

	void Init()
	{
		m_WeaponDataCustom[0].Init();
		m_WeaponDataCustom[1].Init();
	}

	~custom_weapon_info() = default;

	struct custom_weapon_data
	{
		float m_flProjectileGravity;
		float m_flProjectileSpread;

		void Init()
		{
			m_flProjectileGravity = 0.0f;
			m_flProjectileSpread = 0.0f;
		}
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

	void Parse_nonvirtual(KeyValues *pKeyValuesData, const char *szWeaponName)
	{
		custom_weapon_info::Parse(pKeyValuesData, szWeaponName);
	}
};

ConVar *mp_forceactivityset{nullptr};

custom_weapon_info *get_wpn_info(void *pweapon)
{
	return (custom_weapon_info *)(*reinterpret_cast<CTFWeaponInfo **>(reinterpret_cast<unsigned char *>(pweapon) + CTFWeaponBase_m_pWeaponInfo_offset));
}

int get_wpn_mode(void *pweapon)
{
	return *reinterpret_cast<int *>(reinterpret_cast<unsigned char *>(pweapon) + CTFWeaponBase_m_iWeaponMode_offset);
}

int CBaseEntityFireBullets = -1;

struct FireBulletsInfo_t;

int m_iEFlagsOffset = -1;
int m_vecOriginOffset = -1;
int m_flSimulationTimeOffset = -1;

float k_flMaxEntityPosCoord = MAX_COORD_FLOAT;

enum InvalidatePhysicsBits_t
{
	POSITION_CHANGED	= 0x1,
	ANGLES_CHANGED		= 0x2,
	VELOCITY_CHANGED	= 0x4,
	ANIMATION_CHANGED	= 0x8,
};

inline bool IsEntityPositionReasonable( const Vector &v )
{
	float r = k_flMaxEntityPosCoord;
	return
		v.x > -r && v.x < r &&
		v.y > -r && v.y < r &&
		v.z > -r && v.z < r;
}

void SetEdictStateChanged(CBaseEntity *pEntity, int offset);

int m_hMoveChildOffset = -1;
int m_hMoveParentOffset = -1;
int m_hMovePeerOffset = -1;
int m_iParentAttachmentOffset = -1;
int m_vecAbsOriginOffset = -1;
int m_spawnflagsOffset = -1;
int m_iClassnameOffset = -1;
int CBaseEntityTouch = -1;
int m_fEffectsOffset = -1;

void *CBaseEntityCalcAbsolutePosition{nullptr};

class CBasePlayer;

class CBaseEntity : public IServerEntity
{
public:
	int entindex()
	{
		return gamehelpers->EntityToBCompatRef(this);
	}

	const char *GetClassname()
	{
		return gamehelpers->GetEntityClassname(this);
	}

	void FireBullets( const FireBulletsInfo_t &info )
	{
		call_vfunc<void, CBaseEntity, const FireBulletsInfo_t &>(this, CBaseEntityFireBullets, info);
	}

	int GetIEFlags()
	{
		if(m_iEFlagsOffset == -1) {
			sm_datatable_info_t info{};
			datamap_t *pMap = gamehelpers->GetDataMap(this);
			gamehelpers->FindDataMapInfo(pMap, "m_iEFlags", &info);
			m_iEFlagsOffset = info.actual_offset;
		}
		
		return *(int *)((unsigned char *)this + m_iEFlagsOffset);
	}

	void DispatchUpdateTransmitState()
	{

	}

	void AddIEFlags(int flags)
	{
		if(m_iEFlagsOffset == -1) {
			sm_datatable_info_t info{};
			datamap_t *pMap = gamehelpers->GetDataMap(this);
			gamehelpers->FindDataMapInfo(pMap, "m_iEFlags", &info);
			m_iEFlagsOffset = info.actual_offset;
		}

		*(int *)((unsigned char *)this + m_iEFlagsOffset) |= flags;

		if ( flags & ( EFL_FORCE_CHECK_TRANSMIT | EFL_IN_SKYBOX ) )
		{
			DispatchUpdateTransmitState();
		}
	}

	void AddSpawnFlags(int flags)
	{
		if(m_spawnflagsOffset == -1) {
			sm_datatable_info_t info{};
			datamap_t *pMap = gamehelpers->GetDataMap(this);
			gamehelpers->FindDataMapInfo(pMap, "m_spawnflags", &info);
			m_spawnflagsOffset = info.actual_offset;
		}

		*(int *)((unsigned char *)this + m_spawnflagsOffset) |= flags;
	}

	bool IsMarkedForDeletion( void ) 
	{
		return (GetIEFlags() & EFL_KILLME);
	}

	void SetSimulationTime(float time)
	{
		if(m_flSimulationTimeOffset == -1) {
			sm_datatable_info_t info{};
			datamap_t *pMap = gamehelpers->GetDataMap(this);
			gamehelpers->FindDataMapInfo(pMap, "m_flSimulationTime", &info);
			m_flSimulationTimeOffset = info.actual_offset;
		}

		*(float *)((unsigned char *)this + m_flSimulationTimeOffset) = time;
		SetEdictStateChanged(this, m_flSimulationTimeOffset);
	}

	CBaseEntity *FirstMoveChild()
	{
		if(m_hMoveChildOffset == -1) {
			datamap_t *map = gamehelpers->GetDataMap(this);
			sm_datatable_info_t info{};
			gamehelpers->FindDataMapInfo(map, "m_hMoveChild", &info);
			m_hMoveChildOffset = info.actual_offset;
		}

		return (*(EHANDLE *)(((unsigned char *)this) + m_hMoveChildOffset)).Get();
	}

	CBaseEntity *GetMoveParent()
	{
		if(m_hMoveParentOffset == -1) {
			datamap_t *map = gamehelpers->GetDataMap(this);
			sm_datatable_info_t info{};
			gamehelpers->FindDataMapInfo(map, "m_hMoveParent", &info);
			m_hMoveParentOffset = info.actual_offset;
		}

		return (*(EHANDLE *)(((unsigned char *)this) + m_hMoveParentOffset)).Get();
	}

	CBaseEntity *NextMovePeer()
	{
		if(m_hMovePeerOffset == -1) {
			datamap_t *map = gamehelpers->GetDataMap(this);
			sm_datatable_info_t info{};
			gamehelpers->FindDataMapInfo(map, "m_hMovePeer", &info);
			m_hMovePeerOffset = info.actual_offset;
		}

		return (*(EHANDLE *)(((unsigned char *)this) + m_hMovePeerOffset)).Get();
	}

	int GetParentAttachment()
	{
		if(m_iParentAttachmentOffset == -1) {
			datamap_t *map = gamehelpers->GetDataMap(this);
			sm_datatable_info_t info{};
			gamehelpers->FindDataMapInfo(map, "m_iParentAttachment", &info);
			m_iParentAttachmentOffset = info.actual_offset;
		}

		return *(unsigned char *)(((unsigned char *)this) + m_iParentAttachmentOffset);
	}

	CCollisionProperty		*CollisionProp() { return (CCollisionProperty		*)GetCollideable(); }
	const CCollisionProperty*CollisionProp() const { return (const CCollisionProperty*)const_cast<CBaseEntity *>(this)->GetCollideable(); }

	CServerNetworkProperty *NetworkProp() { return (CServerNetworkProperty *)GetNetworkable(); }
	const CServerNetworkProperty *NetworkProp() const { return (const CServerNetworkProperty *)const_cast<CBaseEntity *>(this)->GetNetworkable(); }

	void InvalidatePhysicsRecursive( int nChangeFlags )
	{
		// Main entry point for dirty flag setting for the 90% case
		// 1) If the origin changes, then we have to update abstransform, Shadow projection, PVS, KD-tree, 
		//    client-leaf system.
		// 2) If the angles change, then we have to update abstransform, Shadow projection,
		//    shadow render-to-texture, client-leaf system, and surrounding bounds. 
		//	  Children have to additionally update absvelocity, KD-tree, and PVS.
		//	  If the surrounding bounds actually update, when we also need to update the KD-tree and the PVS.
		// 3) If it's due to attachment, then all children who are attached to an attachment point
		//    are assumed to have dirty origin + angles.

		// Other stuff:
		// 1) Marking the surrounding bounds dirty will automatically mark KD tree + PVS dirty.
		
		int nDirtyFlags = 0;

		if ( nChangeFlags & VELOCITY_CHANGED )
		{
			nDirtyFlags |= EFL_DIRTY_ABSVELOCITY;
		}

		if ( nChangeFlags & POSITION_CHANGED )
		{
			nDirtyFlags |= EFL_DIRTY_ABSTRANSFORM;

	#ifndef CLIENT_DLL
			NetworkProp()->MarkPVSInformationDirty();
	#endif

			// NOTE: This will also mark shadow projection + client leaf dirty
			CollisionProp()->MarkPartitionHandleDirty();
		}

		// NOTE: This has to be done after velocity + position are changed
		// because we change the nChangeFlags for the child entities
		if ( nChangeFlags & ANGLES_CHANGED )
		{
			nDirtyFlags |= EFL_DIRTY_ABSTRANSFORM;
			if ( CollisionProp()->DoesRotationInvalidateSurroundingBox() )
			{
				// NOTE: This will handle the KD-tree, surrounding bounds, PVS
				// render-to-texture shadow, shadow projection, and client leaf dirty
				CollisionProp()->MarkSurroundingBoundsDirty();
			}
			else
			{
	#ifdef CLIENT_DLL
				MarkRenderHandleDirty();
				g_pClientShadowMgr->AddToDirtyShadowList( this );
				g_pClientShadowMgr->MarkRenderToTextureShadowDirty( GetShadowHandle() );
	#endif
			}

			// This is going to be used for all children: children
			// have position + velocity changed
			nChangeFlags |= POSITION_CHANGED | VELOCITY_CHANGED;
		}

		AddIEFlags( nDirtyFlags );

		// Set flags for children
		bool bOnlyDueToAttachment = false;
		if ( nChangeFlags & ANIMATION_CHANGED )
		{
	#ifdef CLIENT_DLL
			g_pClientShadowMgr->MarkRenderToTextureShadowDirty( GetShadowHandle() );
	#endif

			// Only set this flag if the only thing that changed us was the animation.
			// If position or something else changed us, then we must tell all children.
			if ( !( nChangeFlags & (POSITION_CHANGED | VELOCITY_CHANGED | ANGLES_CHANGED) ) )
			{
				bOnlyDueToAttachment = true;
			}

			nChangeFlags = POSITION_CHANGED | ANGLES_CHANGED | VELOCITY_CHANGED;
		}

		for (CBaseEntity *pChild = FirstMoveChild(); pChild; pChild = pChild->NextMovePeer())
		{
			// If this is due to the parent animating, only invalidate children that are parented to an attachment
			// Entities that are following also access attachments points on parents and must be invalidated.
			if ( bOnlyDueToAttachment )
			{
	#ifdef CLIENT_DLL
				if ( (pChild->GetParentAttachment() == 0) && !pChild->IsFollowingEntity() )
					continue;
	#else
				if ( pChild->GetParentAttachment() == 0 )
					continue;
	#endif
			}
			pChild->InvalidatePhysicsRecursive( nChangeFlags );
		}

		//
		// This code should really be in here, or the bone cache should not be in world space.
		// Since the bone transforms are in world space, if we move or rotate the entity, its
		// bones should be marked invalid.
		//
		// As it is, we're near ship, and don't have time to setup a good A/B test of how much
		// overhead this fix would add. We've also only got one known case where the lack of
		// this fix is screwing us, and I just fixed it, so I'm leaving this commented out for now.
		//
		// Hopefully, we'll put the bone cache in entity space and remove the need for this fix.
		//
		//#ifdef CLIENT_DLL
		//	if ( nChangeFlags & (POSITION_CHANGED | ANGLES_CHANGED | ANIMATION_CHANGED) )
		//	{
		//		C_BaseAnimating *pAnim = GetBaseAnimating();
		//		if ( pAnim )
		//			pAnim->InvalidateBoneCache();		
		//	}
		//#endif
	}

	void CalcAbsolutePosition()
	{
		call_mfunc<void, CBaseEntity>(this, CBaseEntityCalcAbsolutePosition);
	}

	const Vector &GetLocalOrigin()
	{
		if(m_vecOriginOffset == -1) {
			sm_datatable_info_t info{};
			datamap_t *pMap = gamehelpers->GetDataMap(this);
			gamehelpers->FindDataMapInfo(pMap, "m_vecOrigin", &info);
			m_vecOriginOffset = info.actual_offset;
		}
		
		return *(Vector *)(((unsigned char *)this) + m_vecOriginOffset);
	}

	const Vector &GetAbsOrigin()
	{
		if(m_vecAbsOriginOffset == -1) {
			datamap_t *map = gamehelpers->GetDataMap(this);
			sm_datatable_info_t info{};
			gamehelpers->FindDataMapInfo(map, "m_vecAbsOrigin", &info);
			m_vecAbsOriginOffset = info.actual_offset;
		}
		
		if(GetIEFlags() & EFL_DIRTY_ABSTRANSFORM) {
			CalcAbsolutePosition();
		}
		
		return *(Vector *)(((unsigned char *)this) + m_vecAbsOriginOffset);
	}

	void SetLocalOrigin( const Vector& origin )
	{
		if(m_vecOriginOffset == -1) {
			sm_datatable_info_t info{};
			datamap_t *pMap = gamehelpers->GetDataMap(this);
			gamehelpers->FindDataMapInfo(pMap, "m_vecOrigin", &info);
			m_vecOriginOffset = info.actual_offset;
		}

		// Safety check against NaN's or really huge numbers
		if ( !IsEntityPositionReasonable( origin ) )
		{
			return;
		}

	//	if ( !origin.IsValid() )
	//	{
	//		AssertMsg( 0, "Bad origin set" );
	//		return;
	//	}

		if (*(Vector *)((unsigned char *)this + m_vecOriginOffset) != origin)
		{
			InvalidatePhysicsRecursive( POSITION_CHANGED );
			*(Vector *)((unsigned char *)this + m_vecOriginOffset) = origin;
			SetEdictStateChanged(this, m_vecOriginOffset);
			SetSimulationTime( gpGlobals->curtime );
		}
	}

	bool ClassMatches( const char *pszClassOrWildcard )
	{
		if(m_iClassnameOffset == -1) {
			sm_datatable_info_t info{};
			datamap_t *pMap = gamehelpers->GetDataMap(this);
			gamehelpers->FindDataMapInfo(pMap, "m_iClassname", &info);
			m_iClassnameOffset = info.actual_offset;
		}

		if ( IDENT_STRINGS( *(string_t *)((unsigned char *)this + m_iClassnameOffset), pszClassOrWildcard ) )
			return true;

		return false;
	}

	bool ClassMatches( string_t nameStr )
	{
		if(m_iClassnameOffset == -1) {
			sm_datatable_info_t info{};
			datamap_t *pMap = gamehelpers->GetDataMap(this);
			gamehelpers->FindDataMapInfo(pMap, "m_iClassname", &info);
			m_iClassnameOffset = info.actual_offset;
		}

		if ( IDENT_STRINGS( *(string_t *)((unsigned char *)this + m_iClassnameOffset), nameStr ) )
			return true;

		return false;
	}

	void Touch( CBaseEntity *pOther )
	{
		call_vfunc<void, CBaseEntity, CBaseEntity *>(this, CBaseEntityTouch, pOther);
	}

	CBasePlayer *GetPlayer()
	{
		int idx = gamehelpers->EntityToBCompatRef(this);
		if(idx >= 1 && idx <= playerhelpers->GetMaxClients()) {
			return (CBasePlayer *)this;
		} else {
			return nullptr;
		}
	}

	bool IsPlayer()
	{
		int idx = gamehelpers->EntityToBCompatRef(this);
		if(idx >= 1 && idx <= playerhelpers->GetMaxClients()) {
			return true;
		} else {
			return false;
		}
	}

	void RemoveEffects(int nEffects)
	{
		if(m_fEffectsOffset == -1) {
			sm_datatable_info_t info{};
			datamap_t *pMap = gamehelpers->GetDataMap(this);
			gamehelpers->FindDataMapInfo(pMap, "m_fEffects", &info);
			m_fEffectsOffset = info.actual_offset;
		}
		
		*(int *)((unsigned char *)this + m_fEffectsOffset) &= ~nEffects;

		if ( nEffects & EF_NODRAW )
		{
			NetworkProp()->MarkPVSInformationDirty();
			DispatchUpdateTransmitState();
		}
	}
};

void CCollisionProperty::MarkSurroundingBoundsDirty()
{
	GetOuter()->AddIEFlags( EFL_DIRTY_SURROUNDING_COLLISION_BOUNDS );
	MarkPartitionHandleDirty();

#ifdef CLIENT_DLL
	g_pClientShadowMgr->MarkRenderToTextureShadowDirty( GetOuter()->GetShadowHandle() );
#else
	GetOuter()->NetworkProp()->MarkPVSInformationDirty();
#endif
}

void CCollisionProperty::MarkPartitionHandleDirty()
{
	// don't bother with the world
	if ( m_pOuter->entindex() == 0 )
		return;
	
	if ( !(m_pOuter->GetIEFlags() & EFL_DIRTY_SPATIAL_PARTITION) )
	{
		m_pOuter->AddIEFlags( EFL_DIRTY_SPATIAL_PARTITION );
		//s_DirtyKDTree.AddEntity( m_pOuter );
	}

#ifdef CLIENT_DLL
	GetOuter()->MarkRenderHandleDirty();
	g_pClientShadowMgr->AddToDirtyShadowList( GetOuter() );
#endif
}

inline bool FClassnameIs(CBaseEntity *pEntity, const char *szClassname)
{ 
	return pEntity->ClassMatches(szClassname); 
}

void SetEdictStateChanged(CBaseEntity *pEntity, int offset)
{
	IServerNetworkable *pNet = pEntity->GetNetworkable();
	edict_t *edict = pNet->GetEdict();
	
	gamehelpers->SetEdictStateChanged(edict, offset);
}

int CBaseCombatWeaponGetSubType = -1;
int CBaseCombatWeaponSetSubType = -1;

class CBaseCombatWeapon : public CBaseEntity
{
public:
	int GetSubType( void )
	{
		return call_vfunc<int, CBaseCombatWeapon>(this, CBaseCombatWeaponGetSubType);
	}

	void SetSubType( int iType )
	{
		call_vfunc<void, CBaseCombatWeapon, int>(this, CBaseCombatWeaponSetSubType, iType);
	}
};

typedef CHandle<CBaseCombatWeapon> CBaseCombatWeaponHandle;

int m_hMyWeaponsOffset = -1;
int CBaseCombatCharacterWeapon_Equip = -1;

class CBaseCombatCharacter : public CBaseEntity
{
public:
	CBaseCombatWeaponHandle *GetMyWeapons()
	{
		if(m_hMyWeaponsOffset == -1) {
			sm_datatable_info_t info{};
			datamap_t *pMap = gamehelpers->GetDataMap(this);
			gamehelpers->FindDataMapInfo(pMap, "m_hMyWeapons", &info);
			m_hMyWeaponsOffset = info.actual_offset;
		}
		
		return *(CBaseCombatWeaponHandle **)((unsigned char *)this + m_hMyWeaponsOffset);
	}

	CBaseCombatWeapon* Weapon_OwnsThisType( const char *pszWeapon, int iSubType )
	{
		// Check for duplicates
		for (int i=0;i<MAX_WEAPONS;i++) 
		{
			if ( GetMyWeapons()[i].Get() && FClassnameIs( GetMyWeapons()[i], pszWeapon ) )
			{
				// Make sure it matches the subtype
				if ( GetMyWeapons()[i]->GetSubType() == iSubType )
					return GetMyWeapons()[i];
			}
		}
		return NULL;
	}

	void Weapon_Equip( CBaseCombatWeapon *pWeapon )
	{
		call_vfunc<void, CBaseCombatCharacter, CBaseCombatWeapon *>(this, CBaseCombatCharacterWeapon_Equip, pWeapon);
	}
};

CBaseEntity *CreateEntityByName( const char *szName )
{
	return servertools->CreateEntityByName(szName);
}

int DispatchSpawn( CBaseEntity *pEntity )
{
	servertools->DispatchSpawn(pEntity);
	return 0;
}

void RemoveEntity( CBaseEntity *pEntity )
{
	servertools->RemoveEntity( pEntity );
}

class CTFWeaponBase : public CBaseCombatWeapon
{
};

class CTFWeaponBaseGun : public CTFWeaponBase
{
};

class CTFDroppedWeapon : public CBaseEntity
{
};

DETOUR_DECL_MEMBER0(CTFWeaponBaseGunGetProjectileSpeed, float)
{
	custom_weapon_info *m_pWeaponInfo = get_wpn_info(this);
	if(!m_pWeaponInfo) {
		return 0.0f;
	}

	int m_iWeaponMode = get_wpn_mode(this);
	return m_pWeaponInfo->m_WeaponData[m_iWeaponMode].m_flProjectileSpeed;
}

DETOUR_DECL_MEMBER0(CTFWeaponBaseGunGetProjectileGravity, float)
{
	custom_weapon_info *m_pWeaponInfo = get_wpn_info(this);
	if(!m_pWeaponInfo) {
		return 0.0f;
	}

	int m_iWeaponMode = get_wpn_mode(this);
	return m_pWeaponInfo->m_WeaponDataCustom[m_iWeaponMode].m_flProjectileGravity;
}

DETOUR_DECL_MEMBER0(CTFWeaponBaseGunGetProjectileSpread, float)
{
	custom_weapon_info *m_pWeaponInfo = get_wpn_info(this);
	if(!m_pWeaponInfo) {
		return 0.0;
	}

	int m_iWeaponMode = get_wpn_mode(this);
	return m_pWeaponInfo->m_WeaponDataCustom[m_iWeaponMode].m_flProjectileSpread;
}

#include <sourcehook/sh_memory.h>

static void *CTFWeaponInfoCTOR{nullptr};
static int FileWeaponInfo_tParse_offset{-1};

static bool weapon_info_vtable_set{false};

DETOUR_DECL_STATIC0(CreateWeaponInfo, FileWeaponInfo_t *)
{
	custom_weapon_info *block{(custom_weapon_info *)calloc(sizeof(custom_weapon_info), 1)};
	call_mfunc<void, CTFWeaponInfo>(block, CTFWeaponInfoCTOR);
	if(!weapon_info_vtable_set) {
		void **vtabl = *(void ***)block;
		SourceHook::SetMemAccess(vtabl, (FileWeaponInfo_tParse_offset * sizeof(void *)) + 4, SH_MEM_READ|SH_MEM_WRITE|SH_MEM_EXEC);
		vtabl[FileWeaponInfo_tParse_offset] = func_to_void(&custom_weapon_info::Parse_nonvirtual);
		weapon_info_vtable_set = true;
	}
	block->Init();
	return block;
}

static CUtlDict< FileWeaponInfo_t*, unsigned short > *WeaponInfoDatabase()
{
	return reinterpret_cast<CUtlDict< FileWeaponInfo_t*, unsigned short > *>(m_WeaponInfoDatabaseAddr);
}

static void remove_weapon_info(const char *name, unsigned short idx)
{
	custom_weapon_info *block{reinterpret_cast<custom_weapon_info *>((*WeaponInfoDatabase())[idx])};
	delete block;
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

static char last_script_name[PATH_MAX];
static char last_script_path[PATH_MAX];

CBaseEntity *last_weapon = nullptr;

static bool force_parse = false;

static WEAPON_FILE_INFO_HANDLE tmp_info_handle;

WEAPON_FILE_INFO_HANDLE GetInvalidWeaponInfoHandle( void )
{
	return (WEAPON_FILE_INFO_HANDLE)WeaponInfoDatabase()->InvalidIndex();
}

const char *translate_weapon_script(const char *szWeaponName, const char **szWeaponScript)
{
	if(szWeaponScript) {
		*szWeaponScript = nullptr;
	}

	if(get_weapon_script->GetFunctionCount() > 0) {
		get_weapon_script->PushCell(last_weapon ? gamehelpers->EntityToBCompatRef(last_weapon) : -1);
		get_weapon_script->PushStringEx((char *)szWeaponName, strlen(szWeaponName)+1, SM_PARAM_STRING_COPY|SM_PARAM_STRING_UTF8, 0);
		get_weapon_script->PushStringEx(last_script_path, sizeof(last_script_path), SM_PARAM_STRING_UTF8, SM_PARAM_COPYBACK);
		get_weapon_script->PushCell(sizeof(last_script_path));
		cell_t res = 0;
		get_weapon_script->Execute(&res);

		switch(res) {
			case Pl_Changed: {
				if(szWeaponScript) {
					*szWeaponScript = last_script_path;
				}
				Q_FileBase( last_script_path, last_script_name, sizeof(last_script_name) );
				szWeaponName = last_script_name;
			} break;
			case Pl_Handled:
			case Pl_Stop: {
				return nullptr;
			}
		}
	}

	return szWeaponName;
}

DETOUR_DECL_MEMBER0(CBaseCombatWeaponGetName, const char *)
{
	return gamehelpers->GetEntityClassname((CBaseEntity *)this);
}

bool ReadWeaponDataFromFileForSlotKV( KeyValues *pKV, const char *szWeaponName, WEAPON_FILE_INFO_HANDLE *phandle )
{
	if(!phandle) {
		phandle = &tmp_info_handle;
	}

	szWeaponName = translate_weapon_script(szWeaponName, nullptr);
	if(!szWeaponName) {
		*phandle = GetInvalidWeaponInfoHandle();
		return false;
	}

	*phandle = FindWeaponInfoSlot( szWeaponName );
	FileWeaponInfo_t *pFileInfo = GetFileWeaponInfoFromHandle( *phandle );

	if ( pFileInfo->bParsedScript ) {
		if(!force_parse) {
			return true;
		}

		pFileInfo->bParsedScript = false;
	}

	if ( !pKV ) {
		return false;
	}

	pFileInfo->Parse( pKV, szWeaponName );

	return true;
}

static bool g_bInWeaponsParse{false};

#include "mathlib/IceKey.H"

void UTIL_DecodeICE( unsigned char * buffer, int size, const unsigned char *key)
{
	if ( !key )
		return;

	IceKey ice( 0 ); // level 0 = 64bit key
	ice.set( key ); // set key

	int blockSize = ice.blockSize();

	unsigned char *temp = (unsigned char *)alloca( PAD_NUMBER( size, blockSize ) );
	unsigned char *p1 = buffer;
	unsigned char *p2 = temp;
				
	// encrypt data in 8 byte blocks
	int bytesLeft = size;
	while ( bytesLeft >= blockSize )
	{
		ice.decrypt( p1, p2 );
		bytesLeft -= blockSize;
		p1+=blockSize;
		p2+=blockSize;
	}

	// copy encrypted data back to original buffer
	Q_memcpy( buffer, temp, size-bytesLeft );
}

KeyValues* ReadEncryptedKVFile( IFileSystem *pFilesystem, const char *szFilenameWithoutExtension, const unsigned char *pICEKey, bool bForceReadEncryptedFile /*= false*/ )
{
	Assert( strchr( szFilenameWithoutExtension, '.' ) == NULL );
	char szFullName[512];

	// Open the weapon data file, and abort if we can't
	KeyValues *pKV = new KeyValues( "WeaponDatafile" );

	Q_snprintf(szFullName,sizeof(szFullName), "%s.txt", szFilenameWithoutExtension);

	g_bInWeaponsParse = true;
	bool loaded_unencrypted = bForceReadEncryptedFile ? false : pKV->LoadFromFile( pFilesystem, szFullName, "WEAPONS" );
	g_bInWeaponsParse = false;

	if ( bForceReadEncryptedFile || !loaded_unencrypted ) // try to load the normal .txt file first
	{
#ifndef _XBOX
		if ( pICEKey )
		{
			Q_snprintf(szFullName,sizeof(szFullName), "scripts/%s.ctx", szFilenameWithoutExtension); // fall back to the .ctx file

			FileHandle_t f = pFilesystem->Open( szFullName, "rb", "MOD" );

			if (!f)
			{
				pKV->deleteThis();
				return NULL;
			}
			// load file into a null-terminated buffer
			int fileSize = pFilesystem->Size(f);
			char *buffer = (char*)MemAllocScratch(fileSize + 1);
		
			Assert(buffer);
		
			pFilesystem->Read(buffer, fileSize, f); // read into local buffer
			buffer[fileSize] = 0; // null terminate file as EOF
			pFilesystem->Close( f );	// close file after reading

			UTIL_DecodeICE( (unsigned char*)buffer, fileSize, pICEKey );

			bool retOK = pKV->LoadFromBuffer( szFullName, buffer, pFilesystem );

			MemFreeScratch();

			if ( !retOK )
			{
				pKV->deleteThis();
				return NULL;
			}
		}
		else
		{
			pKV->deleteThis();
			return NULL;
		}
#else
		pKV->deleteThis();
		return NULL;
#endif
	}

	return pKV;
}

DETOUR_DECL_STATIC1(LookupWeaponInfoSlot, WEAPON_FILE_INFO_HANDLE, const char *,name)
{
	name = translate_weapon_script(name, nullptr);
	if(!name) {
		return GetInvalidWeaponInfoHandle();
	}

	return WeaponInfoDatabase()->Find( name );
}

DETOUR_DECL_STATIC4(ReadWeaponDataFromFileForSlot, bool, IFileSystem*, pFilesystem, const char *, szWeaponName, WEAPON_FILE_INFO_HANDLE *, phandle, const unsigned char *, pICEKey)
{
	if(!phandle) {
		phandle = &tmp_info_handle;
	}

	const char *szWeaponScript{nullptr};
	szWeaponName = translate_weapon_script(szWeaponName, &szWeaponScript);
	if(!szWeaponName) {
		*phandle = GetInvalidWeaponInfoHandle();
		return false;
	}

	*phandle = FindWeaponInfoSlot( szWeaponName );
	FileWeaponInfo_t *pFileInfo = GetFileWeaponInfoFromHandle( *phandle );

	if ( pFileInfo->bParsedScript ) {
		if(!force_parse) {
			return true;
		}

		pFileInfo->bParsedScript = false;
	}

	char sz[128];
	if(szWeaponScript) {
		Q_snprintf( sz, sizeof( sz ), "%s", szWeaponScript );
		V_StripExtension( sz, sz, sizeof(sz) );
	} else {
		Q_snprintf( sz, sizeof( sz ), "%s", szWeaponName );
	}

	KeyValues *pKV = ReadEncryptedKVFile( pFilesystem, sz, pICEKey,
#if defined( DOD_DLL )
		true			// Only read .ctx files!
#else
		false
#endif
		);

	if ( !pKV ) {
		return false;
	}

	pFileInfo->Parse( pKV, szWeaponName );

	pKV->deleteThis();

	return true;
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

	const unsigned char *pICEKey = g_pGameRules ? call_vfunc<const unsigned char *>(g_pGameRules, GetEncryptionKeyoffset) : nullptr;

	force_parse = force;
	bool added = ReadWeaponDataFromFileForSlot(filesystem, fileBase, &tmp_info_handle, pICEKey);
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

	force_parse = force;
	bool added = ReadWeaponDataFromFileForSlotKV(pKV, fileBase, &tmp_info_handle);
	force_parse = false;
	return added;
}

cell_t get_ammo_index(IPluginContext *pContext, const cell_t *params)
{
	char *name = nullptr;
	pContext->LocalToString(params[1], &name);

	return GetAmmoDef()->Index(name);
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

	CAmmoDef *def = GetAmmoDef();

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

	CAmmoDef *def = GetAmmoDef();

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
	CAmmoDef *def = GetAmmoDef();

	return def->m_nAmmoIndex;
}

cell_t weapon_info_count(IPluginContext *pContext, const cell_t *params)
{
	return WeaponInfoDatabase()->Count();
}

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

void addr_read_vec(cell_t *&addr, const Vector &vec)
{
	*addr = sp_ftoc(vec.x);
	++addr;

	*addr = sp_ftoc(vec.y);
	++addr;

	*addr = sp_ftoc(vec.z);
	++addr;
}

enum AmmoTracer_t
{
	TRACER_INVALID = -1,

	TRACER_NONE = 0,
	TRACER_LINE,
	TRACER_RAIL,
	TRACER_BEAM,
	TRACER_LINE_AND_WHIZ,

	//CUSTOM ADDED
	TRACER_PARTICLE,
	TRACER_PARTICLE_AND_WHIZ,
};

#define MAX_TRACER_NAME 64
#define MAX_TRACER_NAME_IN_CELLS (MAX_TRACER_NAME / sizeof(cell_t))

#define TRACER_DEFAULT_ATTACHMENT -2

struct CustomFireBulletsInfo_t : public FireBulletsInfo_t
{
	int attachment{TRACER_DEFAULT_ATTACHMENT};
	std::string tracer_name{};
	AmmoTracer_t forced_tracer_type{TRACER_INVALID};
};

void AddrToBulletInfo(const cell_t *&addr, FireBulletsInfo_t &info)
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

void AddrToCustomBulletInfo(const cell_t *addr, CustomFireBulletsInfo_t &info)
{
	AddrToBulletInfo(static_cast<const cell_t *&>(addr), static_cast<FireBulletsInfo_t &>(info));
	info.attachment = *addr;
	++addr;
	info.tracer_name.assign((const char *)addr, MAX_TRACER_NAME);
	addr += MAX_TRACER_NAME_IN_CELLS;
	info.forced_tracer_type = (AmmoTracer_t)*addr;
	++addr;
}

static CustomFireBulletsInfo_t *current_custombulletinfo{nullptr};

static cell_t BaseEntityFireBullets(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = (CBaseEntity *)gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}
	
	CustomFireBulletsInfo_t info{};
	
	cell_t *addr = nullptr;
	pContext->LocalToPhysAddr(params[2], &addr);

	::AddrToCustomBulletInfo(addr, info);

	current_custombulletinfo = &info;
	pEntity->FireBullets(info);
	current_custombulletinfo = nullptr;

	return 0;
}

void GiveNamedItem(CBaseEntity *pItem, CBaseCombatCharacter *pOwner)
{
	if(!pItem || pOwner->IsPlayer()) {
		return;
	}

	pOwner->Weapon_Equip((CBaseCombatWeapon *)pItem);
}

CBaseEntity	*GiveNamedItem( CBaseCombatCharacter *pOwner, const char *pszName, int iSubType )
{
	// If I already own this type don't create one
	if ( pOwner->Weapon_OwnsThisType(pszName, iSubType) )
		return NULL;

	// Msg( "giving %s\n", pszName );

	EHANDLE pent;

	pent = CreateEntityByName(pszName);
	if ( pent == NULL )
	{
		Msg( "NULL Ent in GiveNamedItem!\n" );
		return NULL;
	}

	pent->SetLocalOrigin( pOwner->GetLocalOrigin() );
	pent->AddSpawnFlags( SF_NORESPAWN );

	CBaseCombatWeapon *pWeapon = dynamic_cast<CBaseCombatWeapon*>( (CBaseEntity*)pent );
	if ( pWeapon )
	{
		pWeapon->SetSubType( iSubType );
	}

	DispatchSpawn( pent );

	if ( pent != NULL && !(pent->IsMarkedForDeletion()) ) 
	{
		pent->Touch( pOwner );
	}

	GiveNamedItem(pent, pOwner);

	return pent;
}

class CEconItemView;

void *CTFPlayerGiveNamedItemPtr{nullptr};

CBaseEntity	*GenerateItemHack( CBaseCombatCharacter *pOwner, const char *pszName, const CEconItemView *pScriptItem )
{
	CBaseEntity *pItem = NULL;

#if 0
	if ( pScriptItem )
	{
		// Generate a weapon directly from that item
		pItem = ItemGeneration()->GenerateItemFromScriptData( pScriptItem, pOwner->GetLocalOrigin(), vec3_angle, pszName );
	}
	else
	{
		// Generate a base item of the specified type
		CItemSelectionCriteria criteria;
		criteria.SetQuality( AE_NORMAL );
		criteria.BAddCondition( "name", k_EOperator_String_EQ, pszName, true );
		pItem = ItemGeneration()->GenerateRandomItem( &criteria, pOwner->GetAbsOrigin(), vec3_angle );
	}
#endif

	return pItem;
}

static bool g_bIgnoreTranslate{false};

CBaseEntity	*GiveNamedItem( CBaseCombatCharacter *pOwner, const char *pszName, int iSubType, const CEconItemView *pScriptItem, bool bForce )
{
#if 1
	g_bIgnoreTranslate = true;
	CBaseEntity *pItem{call_mfunc<CBaseEntity *, CBaseCombatCharacter, const char *, int, const CEconItemView *, bool>(pOwner, CTFPlayerGiveNamedItemPtr, pszName, iSubType, pScriptItem, bForce)};
	g_bIgnoreTranslate = false;
	GiveNamedItem(pItem, pOwner);
	return pItem;
#else
	// We need to support players putting any shotgun into a shotgun slot, pistol into a pistol slot, etc.
	// For legacy reasons, different classes actually spawn different entities for their shotguns/pistols/etc.
	// To deal with this, we translate entities into the right one for the class we're playing.
#if 0
	if ( !bForce )
	{
		// We don't do this if force is set, since a spy might be disguising as this character, etc.
		pszName = TranslateWeaponEntForClass( pszName, GetPlayerClass()->GetClassIndex() );
	}
#endif

	if ( !pszName )
		return NULL;

	// If I already own this type don't create one
	if ( pOwner->Weapon_OwnsThisType(pszName, iSubType) && !bForce)
	{
		Assert(0);
		return NULL;
	}

	CBaseEntity *pItem = NULL;

	pItem = GenerateItemHack( pOwner, pszName, pScriptItem );

	if ( pItem == NULL )
	{
		Msg( "Failed to generate base item: %s\n", pszName );
		return NULL;
	}

	pItem->AddSpawnFlags( SF_NORESPAWN );

	CBaseCombatWeapon *pWeapon = dynamic_cast<CBaseCombatWeapon*>( (CBaseEntity*)pItem );
	if ( pWeapon )
	{
		pWeapon->SetSubType( iSubType );
	}

	DispatchSpawn( pItem );

	if ( pItem != NULL && !(pItem->IsMarkedForDeletion()) ) 
	{
		pItem->Touch( pOwner );
	}

	GiveNamedItem(pItem, pOwner);

	return pItem;
#endif
}

static cell_t give_weapon(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = (CBaseEntity *)gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}

	char *pszName{nullptr};
	pContext->LocalToString(params[2], &pszName);

	CBaseEntity *pItem{GiveNamedItem((CBaseCombatCharacter *)pEntity, pszName, params[3])};

	return pItem ? gamehelpers->EntityToBCompatRef(pItem) : -1;
}

#include "tf2items.hpp"

static cell_t give_weapon_econ(IPluginContext *pContext, const cell_t *params)
{
	CBaseEntity *pEntity = (CBaseEntity *)gamehelpers->ReferenceToEntity(params[1]);
	if(!pEntity) {
		return pContext->ThrowNativeError("Invalid Entity Reference/Index %i", params[1]);
	}

	TScriptedItemOverride *pScriptedItemOverride = GetScriptedItemOverrideFromHandle(params[2], pContext);
	if (pScriptedItemOverride == NULL)
	{
		return -1;
	}

	bool bForce{false};

	CEconItemView hScriptCreatedItem;
	const char *strWeaponClassname{init_tf2items_itemview(hScriptCreatedItem, pScriptedItemOverride, bForce)};

	CBaseEntity *pItem{GiveNamedItem((CBaseCombatCharacter *)pEntity, strWeaponClassname, params[3], &hScriptCreatedItem, bForce)};

	return pItem ? gamehelpers->EntityToBCompatRef(pItem) : -1;
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
	{"give_weapon", give_weapon},
	{"give_weapon_econ", give_weapon_econ},
	{NULL, NULL}
};

CBaseEntity *last_client = nullptr;

IForward *translate_weapon_classname = nullptr;

static char last_classname[64];
DETOUR_DECL_STATIC2(TranslateWeaponEntForClass, const char *, const char *, pszName, int, iClass)
{
	if(g_bIgnoreTranslate) {
		return pszName;
	}

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

static bool in_spawn{false};

DETOUR_DECL_MEMBER0(CBaseCombatWeaponPrecache, void)
{
	last_weapon = (CBaseEntity *)this;
	DETOUR_MEMBER_CALL(CBaseCombatWeaponPrecache)();
	if(!in_spawn) {
		last_weapon = nullptr;
	}
}

DETOUR_DECL_MEMBER0(CTFWeaponBaseSpawn, void)
{
	in_spawn = true;
	last_weapon = (CBaseEntity *)this;
	DETOUR_MEMBER_CALL(CTFWeaponBaseSpawn)();
	last_weapon = nullptr;
	in_spawn = false;
}

DETOUR_DECL_MEMBER0(CTFWeaponBaseMeleeSpawn, void)
{
	in_spawn = true;
	last_weapon = (CBaseEntity *)this;
	DETOUR_MEMBER_CALL(CTFWeaponBaseMeleeSpawn)();
	last_weapon = nullptr;
	in_spawn = false;
}

#define DETOUR_DECL_STATIC10(name, ret, p1type, p1name, p2type, p2name, p3type, p3name, p4type, p4name, p5type, p5name, p6type, p6name, p7type, p7name, p8type, p8name, p9type, p9name, p10type, p10name) \
ret (*name##_Actual)(p1type, p2type, p3type, p4type, p5type, p6type, p7type, p8type, p9type, p10type) = NULL; \
ret name(p1type p1name, p2type p2name, p3type p3name, p4type p4name, p5type p5name, p6type p6name, p7type p7name, p8type p8name, p9type p9name, p10type p10name)

DETOUR_DECL_STATIC10(FX_FireBullets, void, CTFWeaponBase *,pWpn, int,iPlayer, const Vector &,vecOrigin, const QAngle &,vecAngles,int,iWeapon, int ,iMode, int, iSeed, float, flSpread, float ,flDamage, bool ,bCritical)
{
	last_weapon = (CBaseEntity *)pWpn;
	DETOUR_STATIC_CALL(FX_FireBullets)(pWpn, iPlayer, vecOrigin, vecAngles, iWeapon, iMode, iSeed, flSpread, flDamage, bCritical);
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
	init_tf2items_vtables(pScriptItem);
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
CDetour *LookupWeaponInfoSlotDetour = nullptr;
CDetour *CBaseCombatWeaponPrecacheDetour = nullptr;
CDetour *CTFWeaponBaseSpawnDetour = nullptr;
CDetour *CTFWeaponBaseMeleeSpawnDetour = nullptr;
CDetour *CTFPlayerItemsMatchDetour = nullptr;
CDetour *CTFPlayerGiveNamedItemDetour = nullptr;
CDetour *CTFPlayerPickupWeaponFromOtherDetour = nullptr;

CDetour *FX_FireBulletsDetour = nullptr;

CDetour *CreateWeaponInfoDetour = nullptr;
CDetour *CTFWeaponBaseGunGetProjectileSpeedDetour = nullptr;
CDetour *CTFWeaponBaseGunGetProjectileGravityDetour = nullptr;
CDetour *CTFWeaponBaseGunGetProjectileSpreadDetour = nullptr;

int CGameRulesGetSkillLevel = -1;
static int CGameRulesIsMultiplayer{-1};

class CGameRules
{
public:
	int GetSkillLevel()
	{
		return call_vfunc<int>(this, CGameRulesGetSkillLevel);
	}

	bool IsMultiplayer()
	{
		return call_vfunc<bool>(this, CGameRulesIsMultiplayer);
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

int CGameRulesGetAmmoQuantityScale = -1;

static const char *g_szGameRulesProxy{nullptr};

CBaseEntity * FindEntityByServerClassname(int iStart, const char * pServerClassName)
{
	constexpr int g_iEdictCount = 2048;
	
	if (iStart >= g_iEdictCount)
		return nullptr;
	for (int i = iStart; i < g_iEdictCount; i++)
	{
		CBaseEntity * pEnt = gamehelpers->ReferenceToEntity(i);
		if (!pEnt)
			continue;
		IServerNetworkable * pNetworkable = ((IServerUnknown *)pEnt)->GetNetworkable();
		if (!pNetworkable)
			continue;
		const char * pName = pNetworkable->GetServerClass()->GetName();
		if (pName && !strcmp(pName, pServerClassName))
			return pEnt;
	}
	return nullptr;
}

static bool baseentity_funcs_detoured{false};

static int CBaseEntityGetTracerAttachment_offset{-1};
static int CBaseEntityMakeTracer_offset{-1};
static int CBaseEntityGetTracerType_offset{-1};
static int CBaseEntityDoImpactEffect_offset{-1};

static CDetour *CBaseEntityGetTracerAttachmentDetour{nullptr};
static CDetour *CBaseEntityMakeTracerDetour{nullptr};
static CDetour *CBaseEntityGetTracerTypeDetour{nullptr};
static CDetour *CBaseEntityDoImpactEffectDetour{nullptr};

static void *UTIL_TracerPtr{nullptr};
static void *UTIL_ParticleTracerPtr{nullptr};

void UTIL_ParticleTracer( const char *pszTracerEffectName, const Vector &vecStart, const Vector &vecEnd, int iEntIndex, int iAttachment, bool bWhiz )
{
	(void_to_func<void(*)(const char *, const Vector &, const Vector &, int, int, bool)>(UTIL_ParticleTracerPtr))(pszTracerEffectName, vecStart, vecEnd, iEntIndex, iAttachment, bWhiz);
}

void UTIL_Tracer( const Vector &vecStart, const Vector &vecEnd, int iEntIndex, int iAttachment, float flVelocity, bool bWhiz, const char *pCustomTracerName, int iParticleID )
{
	(void_to_func<void(*)(const Vector &, const Vector &, int, int, float, bool, const char *, int)>(UTIL_TracerPtr))(vecStart, vecEnd, iEntIndex, iAttachment, flVelocity, bWhiz, pCustomTracerName, iParticleID);
}

IForward *entity_impact_effect = nullptr;

#define IMPACTEFFECTTRACEINFO_SIZE (((4 * 3) * 3) + 4)
#define IMPACTEFFECTTRACEINFO_SIZE_IN_CELLS (IMPACTEFFECTTRACEINFO_SIZE / sizeof(cell_t))

static bool in_player_firebullet{false};
static bool clientside_impacteffect{false};
static int last_damagetype{DMG_GENERIC};

DETOUR_DECL_MEMBER5(CTFPlayerFireBullet, void, CTFWeaponBase *,pWpn, const FireBulletsInfo_t &,info, bool ,bDoEffects, int ,nDamageType, int ,nCustomDamageType)
{
	last_weapon = pWpn;
	in_player_firebullet = true;
	if(!bDoEffects) {
		bDoEffects = true;
		clientside_impacteffect = true;
	}
	last_damagetype = nDamageType;
	DETOUR_MEMBER_CALL(CTFPlayerFireBullet)(pWpn, info, bDoEffects, nDamageType, nCustomDamageType);
	clientside_impacteffect = false;
	in_player_firebullet = false;
	last_weapon = nullptr;
}

static bool on_impact_trace(CBaseEntity *pEntity, trace_t &tr, int nDamageType)
{
	if(entity_impact_effect->GetFunctionCount() > 0) {
		entity_impact_effect->PushCell(pEntity ? gamehelpers->EntityToBCompatRef(pEntity) : -1);
		cell_t data[IMPACTEFFECTTRACEINFO_SIZE_IN_CELLS]{};
		cell_t *dataptr{data};
		addr_read_vec(dataptr, tr.startpos);
		addr_read_vec(dataptr, tr.endpos);
		addr_read_vec(dataptr, tr.plane.normal);
		*dataptr = tr.m_pEnt ? gamehelpers->EntityToBCompatRef(tr.m_pEnt) : -1;
		entity_impact_effect->PushArray(data, IMPACTEFFECTTRACEINFO_SIZE_IN_CELLS, 0);
		entity_impact_effect->PushCell(nDamageType);
		cell_t res = 0;
		entity_impact_effect->Execute(&res);

		switch(res) {
			case Pl_Handled:
			case Pl_Stop: {
				return false;
			}
		}
	}

	return true;
}

DETOUR_DECL_MEMBER2(CTFPlayerImpactWaterTrace, void, trace_t &,trace, const Vector &,vecStart)
{
	if(in_player_firebullet) {
		if(!on_impact_trace(last_weapon, trace, last_damagetype)) {
			return;
		}

		if(clientside_impacteffect) {
			return;
		}
	}

	DETOUR_MEMBER_CALL(CTFPlayerImpactWaterTrace)(trace, vecStart);
}

DETOUR_DECL_STATIC3(UTIL_ImpactTraceCallback, void, trace_t *, tr, int, nDamageType, const char *,pCustomImpactName)
{
	if(in_player_firebullet) {
		if(!on_impact_trace(last_weapon, *tr, nDamageType)) {
			return;
		}

		if(clientside_impacteffect) {
			return;
		}
	}

	DETOUR_STATIC_CALL(UTIL_ImpactTraceCallback)(tr, nDamageType, pCustomImpactName);
}

DETOUR_DECL_MEMBER2(CBaseEntityDoImpactEffect, void, trace_t &, tr, int, nDamageType)
{
	if(!on_impact_trace((CBaseEntity *)this, tr, nDamageType)) {
		return;
	}

	DETOUR_MEMBER_CALL(CBaseEntityDoImpactEffect)(tr, nDamageType);
}

DETOUR_DECL_MEMBER0(CBaseEntityGetTracerType, const char *)
{
	if(current_custombulletinfo != nullptr) {
		if(!current_custombulletinfo->tracer_name.empty()) {
			return current_custombulletinfo->tracer_name.c_str();
		}
	}

	return nullptr;
}

DETOUR_DECL_MEMBER0(CBaseEntityGetTracerAttachment, int)
{
	if(current_custombulletinfo != nullptr) {
		if(current_custombulletinfo->attachment != TRACER_DEFAULT_ATTACHMENT) {
			return current_custombulletinfo->attachment;
		}
	}

	int iAttachment = TRACER_DONT_USE_ATTACHMENT;

	if ( g_pGameRules && g_pGameRules->IsMultiplayer() )
	{
		iAttachment = 1;
	}

	return iAttachment;
}

DETOUR_DECL_MEMBER3(CBaseEntityMakeTracer, void, const Vector &,vecTracerSrc, const trace_t &,tr, int, iTracerType)
{
	CBaseEntity *pThis{(CBaseEntity *)this};

	const char *pszTracerName = ((CBaseEntityGetTracerTypeClass *)this)->CBaseEntityGetTracerType();

	Vector vNewSrc = vecTracerSrc;

	int iAttachment = ((CBaseEntityGetTracerAttachmentClass *)this)->CBaseEntityGetTracerAttachment();

	if(current_custombulletinfo != nullptr) {
		if(current_custombulletinfo->forced_tracer_type != TRACER_INVALID) {
			iTracerType = current_custombulletinfo->forced_tracer_type;
		}
	}

	switch ( iTracerType )
	{
	case TRACER_NONE:
		break;
	case TRACER_RAIL:
		//
		break;
	case TRACER_BEAM:
		//
		break;
	case TRACER_LINE:
		UTIL_Tracer( vNewSrc, tr.endpos, pThis->entindex(), iAttachment, 0.0f, false, pszTracerName );
		break;
	case TRACER_LINE_AND_WHIZ:
		UTIL_Tracer( vNewSrc, tr.endpos, pThis->entindex(), iAttachment, 0.0f, true, pszTracerName );
		break;
	case TRACER_PARTICLE:
		UTIL_ParticleTracer( pszTracerName, vNewSrc, tr.endpos, pThis->entindex(), iAttachment, false );
		break;
	case TRACER_PARTICLE_AND_WHIZ:
		UTIL_ParticleTracer( pszTracerName, vNewSrc, tr.endpos, pThis->entindex(), iAttachment, true );
		break;
	}
}

void Sample::OnCoreMapStart(edict_t * pEdictList, int edictCount, int clientMax)
{
	g_pGameRules = (CGameRules *)g_pSDKTools->GetGameRules();

	if(!baseentity_funcs_detoured) {
		CBaseEntity *gamerules_proxy{FindEntityByServerClassname(0, g_szGameRulesProxy)};
		if(gamerules_proxy) {
			void **vtabl = *(void ***)gamerules_proxy;

			CBaseEntityGetTracerAttachmentDetour = DETOUR_CREATE_MEMBER(CBaseEntityGetTracerAttachment, vtabl[CBaseEntityGetTracerAttachment_offset])
			CBaseEntityGetTracerAttachmentDetour->EnableDetour();

			CBaseEntityMakeTracerDetour = DETOUR_CREATE_MEMBER(CBaseEntityMakeTracer, vtabl[CBaseEntityMakeTracer_offset])
			CBaseEntityMakeTracerDetour->EnableDetour();

			CBaseEntityGetTracerTypeDetour = DETOUR_CREATE_MEMBER(CBaseEntityMakeTracer, vtabl[CBaseEntityGetTracerType_offset])
			CBaseEntityGetTracerTypeDetour->EnableDetour();

			CBaseEntityDoImpactEffectDetour = DETOUR_CREATE_MEMBER(CBaseEntityDoImpactEffect, vtabl[CBaseEntityDoImpactEffect_offset])
			CBaseEntityDoImpactEffectDetour->EnableDetour();

			baseentity_funcs_detoured = true;
		}
	}

	if(!gamerules_vtable_assigned) {
		if(g_pGameRules) {
			void **vtabl = *(void ***)g_pGameRules;

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
	{new ConVar{"tf_plr_dmg_dummy", "0"}, new ConVar{"tf_npc_dmg_dummy", "0"}},
	{new ConVar{"tf_plr_dmg_primary", "6"}, new ConVar{"tf_npc_dmg_primary", "6"}},
	{new ConVar{"tf_plr_dmg_secondary", "15"}, new ConVar{"tf_npc_dmg_secondary", "15"}},
	{new ConVar{"tf_plr_dmg_metal", "0"}, new ConVar{"tf_npc_dmg_metal", "0"}},
	{new ConVar{"tf_plr_dmg_grenade1", "120"}, new ConVar{"tf_npc_dmg_grenade1", "120"}},
	{new ConVar{"tf_plr_dmg_grenade2", "120"}, new ConVar{"tf_npc_dmg_grenade2", "120"}},
	{new ConVar{"tf_plr_dmg_grenade3", "120"}, new ConVar{"tf_npc_dmg_grenade3", "120"}},
};

CDetour *GuessDamageForceDetour{nullptr};

float ImpulseScale( float flTargetMass, float flDesiredSpeed )
{
	return (flTargetMass * flDesiredSpeed);
}

static ConVar *phys_pushscale{nullptr};

void CalculateExplosiveDamageForce( CTakeDamageInfo *info, const Vector &vecDir, const Vector &vecForceOrigin, float flScale )
{
	info->SetDamagePosition( vecForceOrigin );

	float flClampForce = ImpulseScale( 75, 400 );

	// Calculate an impulse large enough to push a 75kg man 4 in/sec per point of damage
	float flForceScale = info->GetBaseDamage() * ImpulseScale( 75, 4 );

	if( flForceScale > flClampForce )
		flForceScale = flClampForce;

	// Fudge blast forces a little bit, so that each
	// victim gets a slightly different trajectory. 
	// This simulates features that usually vary from
	// person-to-person variables such as bodyweight,
	// which are all indentical for characters using the same model.
	flForceScale *= random->RandomFloat( 0.85, 1.15 );

	// Calculate the vector and stuff it into the takedamageinfo
	Vector vecForce = vecDir;
	VectorNormalize( vecForce );
	vecForce *= flForceScale;
	vecForce *= phys_pushscale->GetFloat();
	vecForce *= flScale;
	info->SetDamageForce( vecForce );
}

void CalculateBulletDamageForce( CTakeDamageInfo *info, int iBulletType, const Vector &vecBulletDir, const Vector &vecForceOrigin, float flScale )
{
	info->SetDamagePosition( vecForceOrigin );
	Vector vecForce = vecBulletDir;
	VectorNormalize( vecForce );
	vecForce *= GetAmmoDef()->DamageForce( iBulletType );
	vecForce *= phys_pushscale->GetFloat();
	vecForce *= flScale;
	info->SetDamageForce( vecForce );
}

void CalculateMeleeDamageForce( CTakeDamageInfo *info, const Vector &vecMeleeDir, const Vector &vecForceOrigin, float flScale )
{
	info->SetDamagePosition( vecForceOrigin );

	// Calculate an impulse large enough to push a 75kg man 4 in/sec per point of damage
	float flForceScale = info->GetBaseDamage() * ImpulseScale( 75, 4 );
	Vector vecForce = vecMeleeDir;
	VectorNormalize( vecForce );
	vecForce *= flForceScale;
	vecForce *= phys_pushscale->GetFloat();
	vecForce *= flScale;
	info->SetDamageForce( vecForce );
}

DETOUR_DECL_STATIC4(GuessDamageForce, void, CTakeDamageInfo *,info, const Vector &,vecForceDir, const Vector &,vecForceOrigin, float,flScale)
{
	if ( info->GetDamageType() & DMG_BULLET )
	{
		CalculateBulletDamageForce( info, GetAmmoDef()->Index("TF_AMMO_PRIMARY"), vecForceDir, vecForceOrigin, flScale );
	}
	else if ( info->GetDamageType() & DMG_BLAST )
	{
		CalculateExplosiveDamageForce( info, vecForceDir, vecForceOrigin, flScale );
	}
	else
	{
		CalculateMeleeDamageForce( info, vecForceDir, vecForceOrigin, flScale );
	}
}

CDetour *pKeyValuesLoadFromFile{nullptr};

DETOUR_DECL_MEMBER4(KeyValuesLoadFromFile, bool, IBaseFileSystem *,filesystem, const char *,resourceName, const char *,pathID, bool, refreshCache)
{
	if(g_bInWeaponsParse) {
		pathID = "WEAPONS";
	}
	bool ret{DETOUR_MEMBER_CALL(KeyValuesLoadFromFile)(filesystem, resourceName, pathID, refreshCache)};
	if(!ret) {
		if(g_bInWeaponsParse) {
			pathID = "custom_mod";
			ret = DETOUR_MEMBER_CALL(KeyValuesLoadFromFile)(filesystem, resourceName, pathID, refreshCache);
		}
	}
	return ret;
}

CDetour *CBaseCombatWeaponGetNameDetour{nullptr};
CDetour *CTFPlayerFireBulletDetour{nullptr};
CDetour *CTFPlayerImpactWaterTraceDetour{nullptr};
CDetour *UTIL_ImpactTraceDetour{nullptr};

bool Sample::SDK_OnLoad(char *error, size_t maxlen, bool late)
{
	if(!gameconfs->LoadGameConfigFile("sdktools.games", &g_pGameConf, error, maxlen)) {
		return false;
	}
	
	g_szGameRulesProxy = g_pGameConf->GetKeyValue("GameRulesProxy");
	
	gameconfs->CloseGameConfigFile(g_pGameConf);

	if(!gameconfs->LoadGameConfigFile("wpnhack", &g_pGameConf, error, maxlen)) {
		return false;
	}

	g_pGameConf->GetOffset("CGameRules::GetEncryptionKey", &GetEncryptionKeyoffset);
	if(GetEncryptionKeyoffset == -1) {
		snprintf(error, maxlen, "could not get CGameRules::GetEncryptionKey offset");
		return false;
	}

	g_pGameConf->GetMemSig("GetFileWeaponInfoFromHandle", &GetFileWeaponInfoFromHandleAddr);
	if(GetFileWeaponInfoFromHandleAddr == nullptr) {
		snprintf(error, maxlen, "could not get GetFileWeaponInfoFromHandle address");
		return false;
	}

	g_pGameConf->GetMemSig("m_WeaponInfoDatabase", &m_WeaponInfoDatabaseAddr);
	if(m_WeaponInfoDatabaseAddr == nullptr) {
		snprintf(error, maxlen, "could not get m_WeaponInfoDatabase address");
		return false;
	}

	g_pGameConf->GetMemSig("FileWeaponInfo_t::Parse", &FileWeaponInfo_tParseAddr);
	if(FileWeaponInfo_tParseAddr == nullptr) {
		snprintf(error, maxlen, "could not get FileWeaponInfo_t::Parse address");
		return false;
	}

	g_pGameConf->GetMemSig("CTFWeaponInfo::Parse", &CTFWeaponInfoParseAddr);
	if(CTFWeaponInfoParseAddr == nullptr) {
		snprintf(error, maxlen, "could not get CTFWeaponInfo::Parse address");
		return false;
	}

	g_pGameConf->GetMemSig("CTFWeaponInfo::CTFWeaponInfo", &CTFWeaponInfoCTOR);
	if(CTFWeaponInfoCTOR == nullptr) {
		snprintf(error, maxlen, "could not get CTFWeaponInfo::CTFWeaponInfo address");
		return false;
	}

	g_pGameConf->GetMemSig("GetAmmoDef", &GetAmmoDefAddr);
	if(GetAmmoDefAddr == nullptr) {
		snprintf(error, maxlen, "could not get GetAmmoDef address");
		return false;
	}

	g_pGameConf->GetOffset("CGameRules::GetAmmoQuantityScale", &CGameRulesGetAmmoQuantityScale);
	if(CGameRulesGetAmmoQuantityScale == -1) {
		snprintf(error, maxlen, "could not get CGameRules::GetAmmoQuantityScale offset");
		return false;
	}

	g_pGameConf->GetOffset("CGameRules::GetSkillLevel", &CGameRulesGetSkillLevel);
	if(CGameRulesGetSkillLevel == -1) {
		snprintf(error, maxlen, "could not get CGameRules::GetSkillLevel offset");
		return false;
	}

	g_pGameConf->GetOffset("CBaseEntity::FireBullets", &CBaseEntityFireBullets);
	if(CBaseEntityFireBullets == -1) {
		snprintf(error, maxlen, "could not get CBaseEntity::FireBullets offset");
		return false;
	}

	g_pGameConf->GetOffset("CBaseEntity::Touch", &CBaseEntityTouch);
	if(CBaseEntityTouch == -1) {
		snprintf(error, maxlen, "could not get CBaseEntity::Touch offset");
		return false;
	}

	g_pGameConf->GetMemSig("CBaseEntity::CalcAbsolutePosition", &CBaseEntityCalcAbsolutePosition);
	if(CBaseEntityCalcAbsolutePosition == nullptr) {
		snprintf(error, maxlen, "could not get CBaseEntity::CalcAbsolutePosition address");
		return false;
	}

	g_pGameConf->GetMemSig("CTFPlayer::GiveNamedItem", &CTFPlayerGiveNamedItemPtr);
	if(CTFPlayerGiveNamedItemPtr == nullptr) {
		snprintf(error, maxlen, "could not get CTFPlayer::GiveNamedItem address");
		return false;
	}

	g_pGameConf->GetOffset("CBaseCombatCharacter::Weapon_Equip", &CBaseCombatCharacterWeapon_Equip);
	if(CBaseCombatCharacterWeapon_Equip == -1) {
		snprintf(error, maxlen, "could not get CBaseCombatCharacter::Weapon_Equip offset");
		return false;
	}

	g_pGameConf->GetOffset("CBaseCombatWeapon::GetSubType", &CBaseCombatWeaponGetSubType);
	if(CBaseCombatWeaponGetSubType == -1) {
		snprintf(error, maxlen, "could not get CBaseCombatWeapon::GetSubType offset");
		return false;
	}

	g_pGameConf->GetOffset("CBaseCombatWeapon::SetSubType", &CBaseCombatWeaponSetSubType);
	if(CBaseCombatWeaponSetSubType == -1) {
		snprintf(error, maxlen, "could not get CBaseCombatWeapon::SetSubType offset");
		return false;
	}

	g_pGameConf->GetOffset("CBaseEntity::GetTracerAttachment", &CBaseEntityGetTracerAttachment_offset);
	if(CBaseEntityGetTracerAttachment_offset == -1) {
		snprintf(error, maxlen, "could not get CBaseEntity::GetTracerAttachment offset");
		return false;
	}

	g_pGameConf->GetOffset("CBaseEntity::MakeTracer", &CBaseEntityMakeTracer_offset);
	if(CBaseEntityMakeTracer_offset == -1) {
		snprintf(error, maxlen, "could not get CBaseEntity::MakeTracer offset");
		return false;
	}

	g_pGameConf->GetOffset("CBaseEntity::GetTracerType", &CBaseEntityGetTracerType_offset);
	if(CBaseEntityGetTracerType_offset == -1) {
		snprintf(error, maxlen, "could not get CBaseEntity::GetTracerType offset");
		return false;
	}

	g_pGameConf->GetOffset("CBaseEntity::DoImpactEffect", &CBaseEntityDoImpactEffect_offset);
	if(CBaseEntityDoImpactEffect_offset == -1) {
		snprintf(error, maxlen, "could not get CBaseEntity::DoImpactEffect offset");
		return false;
	}

	g_pGameConf->GetOffset("CGameRules::IsMultiplayer", &CGameRulesIsMultiplayer);
	if(CGameRulesIsMultiplayer == -1) {
		snprintf(error, maxlen, "could not get CGameRules::IsMultiplayer offset");
		return false;
	}

	g_pGameConf->GetMemSig("UTIL_Tracer", &UTIL_TracerPtr);
	if(UTIL_TracerPtr == nullptr) {
		snprintf(error, maxlen, "could not get UTIL_Tracer address");
		return false;
	}

	g_pGameConf->GetMemSig("UTIL_ParticleTracer", &UTIL_ParticleTracerPtr);
	if(UTIL_ParticleTracerPtr == nullptr) {
		snprintf(error, maxlen, "could not get UTIL_ParticleTracer address");
		return false;
	}

	int CTFWeaponBasem_pWeaponInfo{-1};
	g_pGameConf->GetOffset("CTFWeaponBase::m_pWeaponInfo", &CTFWeaponBasem_pWeaponInfo);
	if(CTFWeaponBasem_pWeaponInfo == -1) {
		snprintf(error, maxlen, "could not get CTFWeaponBase::m_pWeaponInfo offset");
		return false;
	}

	int CTFWeaponBasem_iWeaponMode{-1};
	g_pGameConf->GetOffset("CTFWeaponBase::m_iWeaponMode", &CTFWeaponBasem_iWeaponMode);
	if(CTFWeaponBasem_iWeaponMode == -1) {
		snprintf(error, maxlen, "could not get CTFWeaponBase::m_iWeaponMode offset");
		return false;
	}

	CDetourManager::Init(g_pSM->GetScriptingEngine(), g_pGameConf);

	pKeyValuesLoadFromFile = DETOUR_CREATE_MEMBER(KeyValuesLoadFromFile, "KeyValues::LoadFromFile")
	if(!pKeyValuesLoadFromFile) {
		snprintf(error, maxlen, "could not create KeyValues::LoadFromFile detour");
		return false;
	}

	TranslateWeaponEntForClassDetour = DETOUR_CREATE_STATIC(TranslateWeaponEntForClass, "TranslateWeaponEntForClass")
	if(!TranslateWeaponEntForClassDetour) {
		snprintf(error, maxlen, "could not create TranslateWeaponEntForClass detour");
		return false;
	}

	GuessDamageForceDetour = DETOUR_CREATE_STATIC(GuessDamageForce, "GuessDamageForce")
	if(!GuessDamageForceDetour) {
		snprintf(error, maxlen, "could not create GuessDamageForce detour");
		return false;
	}

	ReadWeaponDataFromFileForSlotDetour = DETOUR_CREATE_STATIC(ReadWeaponDataFromFileForSlot, "ReadWeaponDataFromFileForSlot")
	if(!ReadWeaponDataFromFileForSlotDetour) {
		snprintf(error, maxlen, "could not create ReadWeaponDataFromFileForSlot detour");
		return false;
	}

	FX_FireBulletsDetour = DETOUR_CREATE_STATIC(FX_FireBullets, "FX_FireBullets")
	if(!FX_FireBulletsDetour) {
		snprintf(error, maxlen, "could not create FX_FireBullets detour");
		return false;
	}

	LookupWeaponInfoSlotDetour = DETOUR_CREATE_STATIC(LookupWeaponInfoSlot, "LookupWeaponInfoSlot")
	if(!LookupWeaponInfoSlotDetour) {
		snprintf(error, maxlen, "could not create LookupWeaponInfoSlot detour");
		return false;
	}

	CBaseCombatWeaponPrecacheDetour = DETOUR_CREATE_MEMBER(CBaseCombatWeaponPrecache, "CBaseCombatWeapon::Precache")
	if(!CBaseCombatWeaponPrecacheDetour) {
		snprintf(error, maxlen, "could not create CBaseCombatWeapon::Precache detour");
		return false;
	}

	CTFWeaponBaseSpawnDetour = DETOUR_CREATE_MEMBER(CTFWeaponBaseSpawn, "CTFWeaponBase::Spawn")
	if(!CTFWeaponBaseSpawnDetour) {
		snprintf(error, maxlen, "could not create CTFWeaponBase::Spawn detour");
		return false;
	}

	CTFWeaponBaseMeleeSpawnDetour = DETOUR_CREATE_MEMBER(CTFWeaponBaseMeleeSpawn, "CTFWeaponBaseMelee::Spawn")
	if(!CTFWeaponBaseMeleeSpawnDetour) {
		snprintf(error, maxlen, "could not create CTFWeaponBaseMelee::Spawn detour");
		return false;
	}

	CTFPlayerItemsMatchDetour = DETOUR_CREATE_MEMBER(CTFPlayerItemsMatch, "CTFPlayer::ItemsMatch")
	if(!CTFPlayerItemsMatchDetour) {
		snprintf(error, maxlen, "could not create CTFPlayer::ItemsMatch detour");
		return false;
	}

	CTFPlayerGiveNamedItemDetour = DETOUR_CREATE_MEMBER(CTFPlayerGiveNamedItem, "CTFPlayer::GiveNamedItem")
	if(!CTFPlayerGiveNamedItemDetour) {
		snprintf(error, maxlen, "could not create CTFPlayer::GiveNamedItem detour");
		return false;
	}

	CTFPlayerPickupWeaponFromOtherDetour = DETOUR_CREATE_MEMBER(CTFPlayerPickupWeaponFromOther, "CTFPlayer::PickupWeaponFromOther")
	if(!CTFPlayerPickupWeaponFromOtherDetour) {
		snprintf(error, maxlen, "could not create CTFPlayer::PickupWeaponFromOther detour");
		return false;
	}

	CreateWeaponInfoDetour = DETOUR_CREATE_STATIC(CreateWeaponInfo, "CreateWeaponInfo")
	if(!CreateWeaponInfoDetour) {
		snprintf(error, maxlen, "could not create CreateWeaponInfo detour");
		return false;
	}

	CTFWeaponBaseGunGetProjectileSpeedDetour = DETOUR_CREATE_MEMBER(CTFWeaponBaseGunGetProjectileSpeed, "CTFWeaponBaseGun::GetProjectileSpeed")
	if(!CTFWeaponBaseGunGetProjectileSpeedDetour) {
		snprintf(error, maxlen, "could not create CTFWeaponBaseGun::GetProjectileSpeed detour");
		return false;
	}

	CTFWeaponBaseGunGetProjectileGravityDetour = DETOUR_CREATE_MEMBER(CTFWeaponBaseGunGetProjectileGravity, "CTFWeaponBaseGun::GetProjectileGravity")
	if(!CTFWeaponBaseGunGetProjectileGravityDetour) {
		snprintf(error, maxlen, "could not create CTFWeaponBaseGun::GetProjectileGravity detour");
		return false;
	}

	CTFWeaponBaseGunGetProjectileSpreadDetour = DETOUR_CREATE_MEMBER(CTFWeaponBaseGunGetProjectileSpread, "CTFWeaponBaseGun::GetProjectileSpread")
	if(!CTFWeaponBaseGunGetProjectileSpreadDetour) {
		snprintf(error, maxlen, "could not create CTFWeaponBaseGun::GetProjectileSpread detour");
		return false;
	}

	CBaseCombatWeaponGetNameDetour = DETOUR_CREATE_MEMBER(CBaseCombatWeaponGetName, "CBaseCombatWeapon::GetName")
	if(!CBaseCombatWeaponGetNameDetour) {
		snprintf(error, maxlen, "could not create CBaseCombatWeapon::GetName detour");
		return false;
	}

	CTFPlayerFireBulletDetour = DETOUR_CREATE_MEMBER(CTFPlayerFireBullet, "CTFPlayer::FireBullet")
	if(!CTFPlayerFireBulletDetour) {
		snprintf(error, maxlen, "could not create CTFPlayer::FireBullet detour");
		return false;
	}

	CTFPlayerImpactWaterTraceDetour = DETOUR_CREATE_MEMBER(CTFPlayerImpactWaterTrace, "CTFPlayer::ImpactWaterTrace")
	if(!CTFPlayerImpactWaterTraceDetour) {
		snprintf(error, maxlen, "could not create CTFPlayer::ImpactWaterTrace detour");
		return false;
	}

	UTIL_ImpactTraceDetour = DETOUR_CREATE_STATIC(UTIL_ImpactTraceCallback, "UTIL_ImpactTrace")
	if(!UTIL_ImpactTraceDetour) {
		snprintf(error, maxlen, "could not create UTIL_ImpactTrace detour");
		return false;
	}

	UTIL_ImpactTraceDetour->EnableDetour();
	pKeyValuesLoadFromFile->EnableDetour();
	TranslateWeaponEntForClassDetour->EnableDetour();
	GuessDamageForceDetour->EnableDetour();
	ReadWeaponDataFromFileForSlotDetour->EnableDetour();
	FX_FireBulletsDetour->EnableDetour();
	LookupWeaponInfoSlotDetour->EnableDetour();
	CBaseCombatWeaponPrecacheDetour->EnableDetour();
	CTFWeaponBaseSpawnDetour->EnableDetour();
	CTFWeaponBaseMeleeSpawnDetour->EnableDetour();
	CTFPlayerItemsMatchDetour->EnableDetour();
	CTFPlayerGiveNamedItemDetour->EnableDetour();
	CTFPlayerPickupWeaponFromOtherDetour->EnableDetour();
	CreateWeaponInfoDetour->EnableDetour();
	CTFWeaponBaseGunGetProjectileSpeedDetour->EnableDetour();
	CTFWeaponBaseGunGetProjectileGravityDetour->EnableDetour();
	CTFWeaponBaseGunGetProjectileSpreadDetour->EnableDetour();
	CBaseCombatWeaponGetNameDetour->EnableDetour();
	CTFPlayerFireBulletDetour->EnableDetour();
	CTFPlayerImpactWaterTraceDetour->EnableDetour();

	FileWeaponInfo_tParse_offset = vfunc_index(&FileWeaponInfo_t::Parse);

	sm_sendprop_info_t tmp_sp_info;
	gamehelpers->FindSendPropInfo("CTFWeaponBase", "m_flReloadPriorNextFire", &tmp_sp_info);
	CTFWeaponBase_m_pWeaponInfo_offset = tmp_sp_info.actual_offset;
	CTFWeaponBase_m_pWeaponInfo_offset += CTFWeaponBasem_pWeaponInfo;

	gamehelpers->FindSendPropInfo("CTFWeaponBase", "m_iReloadMode", &tmp_sp_info);
	CTFWeaponBase_m_iWeaponMode_offset = tmp_sp_info.actual_offset;
	CTFWeaponBase_m_iWeaponMode_offset -= CTFWeaponBasem_iWeaponMode;

	dictionary = servertools->GetEntityFactoryDictionary();

	g_pEntityList = reinterpret_cast<CBaseEntityList *>(gamehelpers->GetGlobalEntityList());

	char pPath[MAX_PATH];
	smutils->BuildPath(Path_SM, pPath, MAX_PATH, "data/weapons");
	filesystem->AddSearchPath( pPath, "WEAPONS" );
	smutils->BuildPath(Path_SM, pPath, MAX_PATH, "configs/weapons");
	filesystem->AddSearchPath( pPath, "WEAPONS" );
	smutils->BuildPath(Path_Game, pPath, MAX_PATH, "scripts");
	filesystem->AddSearchPath( pPath, "WEAPONS" );
	smutils->BuildPath(Path_Game, pPath, MAX_PATH, nullptr);
	filesystem->AddSearchPath( pPath, "WEAPONS" );
	smutils->BuildPath(Path_Game, pPath, MAX_PATH, "tf2_misc_dir.vpk");
	filesystem->AddSearchPath( pPath, "WEAPONS" );

	translate_weapon_classname = forwards->CreateForward("translate_weapon_classname", ET_Hook, 6, nullptr, Param_Cell, Param_Cell, Param_Cell, Param_String, Param_String, Param_Cell);
	get_weapon_script = forwards->CreateForward("get_weapon_script", ET_Hook, 4, nullptr, Param_Cell, Param_String, Param_String, Param_Cell);

	entity_impact_effect = forwards->CreateForward("entity_impact_effect", ET_Hook, 3, nullptr, Param_Cell, Param_Array, Param_Cell);

	ammo_handle = handlesys->CreateType("ammotype", this, 0, nullptr, nullptr, myself->GetIdentity(), nullptr);

	sharesys->AddInterface(myself, this);
	sharesys->RegisterLibrary(myself, "wpnhack");

	sharesys->AddNatives(myself, natives);

	CAmmoDef *def = GetAmmoDef();

	for ( int i=1; i < TF_AMMO_COUNT; i++ ) {
		def->m_AmmoType[i].pPlrDmgCVar = tf_ammo_dmg_cvars[i].plr_dmg;
		def->m_AmmoType[i].pPlrDmg = USE_CVAR;
		def->m_AmmoType[i].pNPCDmgCVar = tf_ammo_dmg_cvars[i].npc_dmg;
		def->m_AmmoType[i].pNPCDmg = USE_CVAR;
	}

	init_tf2items_handles();

	return true;
}

void Sample::SDK_OnAllLoaded()
{
	SM_GET_LATE_IFACE(SDKTOOLS, g_pSDKTools);
	SM_GET_LATE_IFACE(SDKHOOKS, g_pSDKHooks);

	g_pSDKHooks->AddEntityListener(this);
}

void Sample::SDK_OnUnload()
{
	forwards->ReleaseForward(translate_weapon_classname);
	forwards->ReleaseForward(get_weapon_script);
	forwards->ReleaseForward(entity_impact_effect);

	TranslateWeaponEntForClassDetour->Destroy();
	ReadWeaponDataFromFileForSlotDetour->Destroy();
	LookupWeaponInfoSlotDetour->Destroy();
	CBaseCombatWeaponPrecacheDetour->Destroy();
	CTFPlayerItemsMatchDetour->Destroy();
	CTFPlayerGiveNamedItemDetour->Destroy();
	CTFPlayerPickupWeaponFromOtherDetour->Destroy();

	CreateWeaponInfoDetour->Destroy();
	CTFWeaponBaseGunGetProjectileSpeedDetour->Destroy();
	CTFWeaponBaseGunGetProjectileGravityDetour->Destroy();
	CTFWeaponBaseGunGetProjectileSpreadDetour->Destroy();

	CTFWeaponBaseSpawnDetour->Destroy();
	CTFWeaponBaseMeleeSpawnDetour->Destroy();
	FX_FireBulletsDetour->Destroy();

	CBaseCombatWeaponGetNameDetour->Destroy();

	CTFPlayerFireBulletDetour->Destroy();
	CTFPlayerImpactWaterTraceDetour->Destroy();
	UTIL_ImpactTraceDetour->Destroy();

	if(CBaseEntityGetTracerAttachmentDetour) {
		CBaseEntityGetTracerAttachmentDetour->Destroy();
	}

	if(CBaseEntityMakeTracerDetour) {
		CBaseEntityMakeTracerDetour->Destroy();
	}

	if(CBaseEntityGetTracerTypeDetour) {
		CBaseEntityGetTracerTypeDetour->Destroy();
	}

	if(CBaseEntityDoImpactEffectDetour) {
		CBaseEntityDoImpactEffectDetour->Destroy();
	}

	GuessDamageForceDetour->Destroy();

	pKeyValuesLoadFromFile->Destroy();

	plsys->RemovePluginsListener(this);
	g_pSDKHooks->RemoveEntityListener(this);

	gameconfs->CloseGameConfigFile(g_pGameConf);
}

bool Sample::RegisterConCommandBase(ConCommandBase *pCommand)
{
	META_REGCVAR(pCommand);
	return true;
}

IUniformRandomStream *random{nullptr};

bool Sample::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	gpGlobals = ismm->GetCGlobals();
	GET_V_IFACE_CURRENT(GetEngineFactory, cvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, filesystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetServerFactory, servertools, IServerTools, VSERVERTOOLS_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, random, IUniformRandomStream, VENGINE_SERVER_RANDOM_INTERFACE_VERSION)
	g_pCVar = cvar;
	ConVar_Register(0, this);

	mp_forceactivityset = g_pCVar->FindVar("mp_forceactivityset");
	phys_pushscale = g_pCVar->FindVar("phys_pushscale");

	return true;
}

void Sample::OnHandleDestroy(HandleType_t type, void *object)
{
	if(type == ammo_handle) {
		ammo_handle_obj *obj = (ammo_handle_obj *)object;

		CAmmoDef *def = GetAmmoDef();

		for(int i = obj->index; i < def->m_nAmmoIndex-1; ++i) {
			def->m_AmmoType[i] = std::move(def->m_AmmoType[i+1]);
		}

		--def->m_nAmmoIndex;

		delete obj;
	}
}