#if defined __wpnhack_included
	#endinput
#endif
#define __wpnhack_included

#include <sdkhooks>
#include <tf2>

#if 0
typeset wpnhack_weapon_callbacks
{
};

native void set_weapon_function(int entity, const char[] name, wpnhack_weapon_callbacks func, any data = 0);
#endif

enum WeaponSound_t
{
	EMPTY,
	SINGLE,
	SINGLE_NPC,
	WPN_DOUBLE,
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
	NUM_SHOOT_SOUND_TYPES,
};

static const char __wpnhack_sound_names[][] =
{
	"empty",
	"single_shot",
	"single_shot_npc",
	"double_shot",
	"double_shot_npc",
	"burst",
	"reload",
	"reload_npc",
	"melee_miss",
	"melee_hit",
	"melee_hit_world",
	"special1",
	"special2",
	"special3",
	"taunt",
	"deploy"
};

enum ItemFlags_t
{
	ITEM_FLAG_SELECTONEMPTY =     (1 << 0),
	ITEM_FLAG_NOAUTORELOAD =      (1 << 1),
	ITEM_FLAG_NOAUTOSWITCHEMPTY = (1 << 2),
	ITEM_FLAG_LIMITINWORLD =      (1 << 3),
	ITEM_FLAG_EXHAUSTIBLE =       (1 << 4),
	ITEM_FLAG_DOHITLOCATIONDMG =  (1 << 5),
	ITEM_FLAG_NOAMMOPICKUPS =     (1 << 6),
	ITEM_FLAG_NOITEMPICKUP =      (1 << 7)
};

static const char __wpnhack_itemflag_names[][] =
{
	"ITEM_FLAG_SELECTONEMPTY",
	"ITEM_FLAG_NOAUTORELOAD",
	"ITEM_FLAG_NOAUTOSWITCHEMPTY",
	"ITEM_FLAG_LIMITINWORLD",
	"ITEM_FLAG_EXHAUSTIBLE",
	"ITEM_FLAG_DOHITLOCATIONDMG",
	"ITEM_FLAG_NOAMMOPICKUPS",
	"ITEM_FLAG_NOITEMPICKUP"
};

enum ProjectileType_t
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

static const char __wpnhack_projectile_names[][] =
{
	"projectile_none",
	"projectile_bullet",
	"projectile_rocket",
	"projectile_pipe",
	"projectile_pipe_remote",
	"projectile_syringe",
	"projectile_flare",
	"projectile_jar",
	"projectile_arrow",
	"projectile_flame_rocket",
	"projectile_jar_milk",
	"projectile_healing_bolt",
	"projectile_energy_ball",
	"projectile_energy_ring",
	"projectile_pipe_remote_practice",
	"projectile_cleaver",
	"projectile_sticky_ball",
	"projectile_cannonball",
	"projectile_building_repair_bolt",
	"projectile_festive_arrow",
	"projectile_throwable",
	"projectile_spellfireball",
	"projectile_festive_urine",
	"projectile_festive_healing_bolt",
	"projectile_breadmonster_jarate",
	"projectile_breadmonster_madmilk",
	"projectile_grapplinghook",
	"projectile_sentry_rocket",
	"projectile_bread_monster"
};

enum TFWeaponType_t
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

static const char __wpnhack_weapon_names[][] =
{
	"primary",
	"secondary",
	"melee",
	"grenade",
	"building",
	"pda",
	"item1",
	"item2",
	"",
	"",
	"",
	"",
	""
};

#define WEAPON_NOCLIP -1

methodmap WeaponInfo < KeyValues
{
	public WeaponInfo()
	{
		return view_as<WeaponInfo>(new KeyValues("WeaponData"));
	}

	property KeyValues __kv
	{
		public get()
		{
			return view_as<KeyValues>(this);
		}
	}

	public void SetPrintName(const char[] val)
	{ this.__kv.SetString("printname", val); }

	public void SetViewmodel(const char[] val)
	{ this.__kv.SetString("viewmodel", val); }

	public void SetPlayermodel(const char[] val)
	{ this.__kv.SetString("playermodel", val); }

	public void SetAnimPrefix(const char[] val)
	{ this.__kv.SetString("anim_prefix", val); }

	property int Bucket
	{
		public set(int val)
		{ this.__kv.SetNum("bucket", val); }
	}

	property int BucketPosition
	{
		public set(int val)
		{ this.__kv.SetNum("bucket_position", val); }
	}

	property int ClipSize
	{
		public set(int val)
		{ this.__kv.SetNum("clip_size", val); }
	}

	property int Clip2Size
	{
		public set(int val)
		{ this.__kv.SetNum("clip2_size", val); }
	}

	property int DefaultClip
	{
		public set(int val)
		{ this.__kv.SetNum("default_clip", val); }
	}

	property int DefaultClip2
	{
		public set(int val)
		{ this.__kv.SetNum("default_clip2", val); }
	}

	property int Rumble
	{
		public set(int val)
		{ this.__kv.SetNum("rumble", val); }
	}

	property ItemFlags_t ItemFlags
	{
		public set(ItemFlags_t val)
		{
			this.__kv.SetNum("item_flags", view_as<int>(val));

			for(int i = 0; i < sizeof(__wpnhack_itemflag_names); ++i) {
				this.__kv.SetNum(__wpnhack_itemflag_names[i], (view_as<int>(val & view_as<ItemFlags_t>(1 << i)) != 0) ? 1 : 0);
			}
		}
	}

	property bool ShowUsageHint
	{
		public set(bool val)
		{ this.__kv.SetNum("showusagehint", val ? 1 : 0); }
	}

	property bool AutoSwitchTo
	{
		public set(bool val)
		{ this.__kv.SetNum("autoswitchto", val ? 1 : 0); }
	}

	property bool AutoSwitchFrom
	{
		public set(bool val)
		{ this.__kv.SetNum("autoswitchfrom", val ? 1 : 0); }
	}

	property bool BuiltRightHanded
	{
		public set(bool val)
		{ this.__kv.SetNum("BuiltRightHanded", val ? 1 : 0); }
	}

	property bool AllowFlipping
	{
		public set(bool val)
		{ this.__kv.SetNum("AllowFlipping", val ? 1 : 0); }
	}

	property bool MeleeWeapon
	{
		public set(bool val)
		{ this.__kv.SetNum("MeleeWeapon", val ? 1 : 0); }
	}

	public void SetPrimaryAmmo(const char[] val)
	{ this.__kv.SetString("primary_ammo", val); }

	public void SetSecondaryAmmo(const char[] val)
	{ this.__kv.SetString("secondary_ammo", val); }

	public void SetSound(WeaponSound_t sound, const char[] val)
	{
		if(this.__kv.JumpToKey("SoundData")) {
			this.__kv.SetString(__wpnhack_sound_names[view_as<int>(sound)], val);
			this.__kv.GoBack();
		}
	}
}

methodmap TFWeaponInfo < WeaponInfo
{
	public TFWeaponInfo()
	{
		return view_as<TFWeaponInfo>(new WeaponInfo());
	}

	property bool DoInstantEjectBrass
	{
		public set(bool val)
		{ this.__kv.SetNum("DoInstantEjectBrass", val ? 1 : 0); }
	}

	public void SetBrassModel(const char[] val)
	{ this.__kv.SetString("BrassModel", val); }

	property TFWeaponType_t WeaponType
	{
		public set(TFWeaponType_t val)
		{
			if(val > TF_WPN_TYPE_ITEM2) {
				SetFailState("weapon type unsupported");
			}

			this.__kv.SetString("WeaponType", __wpnhack_weapon_names[view_as<int>(val)]);
		}
	}

	property bool Grenade
	{
		public set(bool val)
		{ this.__kv.SetNum("Grenade", val ? 1 : 0); }
	}

	property float DamageRadius
	{
		public set(float val)
		{ this.__kv.SetFloat("DamageRadius", val); }
	}

	property float PrimerTime
	{
		public set(float val)
		{ this.__kv.SetFloat("PrimerTime", val); }
	}

	property bool PlayGrenTimer
	{
		public set(bool val)
		{ this.__kv.SetNum("PlayGrenTimer", val ? 1 : 0); }
	}

	property bool LowerMainWeapon
	{
		public set(bool val)
		{ this.__kv.SetNum("LowerMainWeapon", val ? 1 : 0); }
	}

	property bool HasTeamSkins_Viewmodel
	{
		public set(bool val)
		{ this.__kv.SetNum("HasTeamSkins_Viewmodel", val ? 1 : 0); }
	}

	property bool HasTeamSkins_Worldmodel
	{
		public set(bool val)
		{ this.__kv.SetNum("HasTeamSkins_Worldmodel", val ? 1 : 0); }
	}

	public void SetMuzzleFlashModel(const char[] val)
	{ this.__kv.SetString("MuzzleFlashModel", val); }

	property float MuzzleFlashModelDuration
	{
		public set(float val)
		{ this.__kv.SetFloat("MuzzleFlashModelDuration", val); }
	}

	public void SetMuzzleFlashParticleEffect(const char[] val)
	{ this.__kv.SetString("MuzzleFlashParticleEffect", val); }

	public void SetTracerEffect(const char[] val)
	{ this.__kv.SetString("TracerEffect", val); }

	public void SetExplosionSound(const char[] val)
	{ this.__kv.SetString("ExplosionSound", val); }

	public void SetExplosionEffect(const char[] val)
	{ this.__kv.SetString("ExplosionEffect", val); }

	public void SetExplosionPlayerEffect(const char[] val)
	{ this.__kv.SetString("ExplosionPlayerEffect", val); }

	public void SetExplosionWaterEffect(const char[] val)
	{ this.__kv.SetString("ExplosionWaterEffect", val); }

	property bool DontDrop
	{
		public set(bool val)
		{ this.__kv.SetNum("DontDrop", val ? 1 : 0); }
	}
}

methodmap TFWeaponData < Handle
{
	public TFWeaponData(TFWeaponInfo info, bool primary)
	{
		DataPack data = new DataPack();
		data.WriteCell(info.__kv);
		data.WriteCell(!primary);
		return view_as<TFWeaponData>(data);
	}

	property KeyValues __kv
	{
		public get()
		{
			DataPack data = view_as<DataPack>(this);
			data.Position = view_as<DataPackPos>(0);
			return view_as<KeyValues>(data.ReadCell());
		}
	}

	property bool __secondary
	{
		public get()
		{
			DataPack data = view_as<DataPack>(this);
			data.Position = view_as<DataPackPos>(1);
			return data.ReadCell() != 0;
		}
	}

	#define __WPNHACK_MAX_SECONDARY_NAME 64

	public void __set_num(const char[] name, int val)
	{
		KeyValues kv = this.__kv;
		if(!this.__secondary) {
			kv.SetNum(name, val);
		} else {
			static char secondary_name[__WPNHACK_MAX_SECONDARY_NAME];
			strcopy(secondary_name, sizeof(secondary_name), "Secondary_");
			StrCat(secondary_name, sizeof(secondary_name), name);
			kv.SetNum(secondary_name, val);
		}
	}

	public void __set_flt(const char[] name, float val)
	{
		KeyValues kv = this.__kv;
		if(!this.__secondary) {
			kv.SetFloat(name, val);
		} else {
			static char secondary_name[__WPNHACK_MAX_SECONDARY_NAME];
			strcopy(secondary_name, sizeof(secondary_name), "Secondary_");
			StrCat(secondary_name, sizeof(secondary_name), name);
			kv.SetFloat(secondary_name, val);
		}
	}

	public void __set_str(const char[] name, const char[] val)
	{
		KeyValues kv = this.__kv;
		if(!this.__secondary) {
			kv.SetString(name, val);
		} else {
			static char secondary_name[__WPNHACK_MAX_SECONDARY_NAME];
			strcopy(secondary_name, sizeof(secondary_name), "Secondary_");
			StrCat(secondary_name, sizeof(secondary_name), name);
			kv.SetString(secondary_name, val);
		}
	}

	property int Damage
	{
		public set(int val)
		{ this.__set_num("Damage", val); }
	}

	property float Range
	{
		public set(float val)
		{ this.__set_flt("Range", val); }
	}

	property int BulletsPerShot
	{
		public set(int val)
		{ this.__set_num("BulletsPerShot", val); }
	}

	property float Spread
	{
		public set(float val)
		{ this.__set_flt("Spread", val); }
	}

	property float PunchAngle
	{
		public set(float val)
		{ this.__set_flt("PunchAngle", val); }
	}

	property float TimeFireDelay
	{
		public set(float val)
		{ this.__set_flt("TimeFireDelay", val); }
	}

	property float TimeIdle
	{
		public set(float val)
		{ this.__set_flt("TimeIdle", val); }
	}

	property float TimeIdleEmpty
	{
		public set(float val)
		{ this.__set_flt("TimeIdleEmpty", val); }
	}

	property float TimeReloadStart
	{
		public set(float val)
		{ this.__set_flt("TimeReloadStart", val); }
	}

	property float TimeReload
	{
		public set(float val)
		{ this.__set_flt("TimeReload", val); }
	}

	property bool DrawCrosshair
	{
		public set(bool val)
		{ this.__set_num("DrawCrosshair", val ? 1 : 0); }
	}

	property int AmmoPerShot
	{
		public set(int val)
		{ this.__set_num("AmmoPerShot", val); }
	}

	property bool UseRapidFireCrits
	{
		public set(bool val)
		{ this.__set_num("UseRapidFireCrits", val ? 1 : 0); }
	}

	property ProjectileType_t ProjectileType
	{
		public set(ProjectileType_t val)
		{ this.__set_str("ProjectileType", __wpnhack_projectile_names[view_as<int>(val)]); }
	}

	property float ProjectileSpeed
	{
		public set(float val)
		{ this.__set_flt("ProjectileSpeed", val); }
	}

	property float SmackDelay
	{
		public set(float val)
		{ this.__set_flt("SmackDelay", val); }
	}
}

forward Action translate_weapon_classname(int client, TFClassType class, int weapon, const char[] old_classname, char[] new_classname, int length);
forward Action get_weapon_script(int weapon, const char[] classname, char[] script, int length);

native int weapon_info_count();

native bool uncache_weapon_file(const char[] name);
native bool precache_weapon_file(const char[] name, bool force);
native bool precache_weapon_kv(KeyValues kv, const char[] name, bool force);

#tryinclude <datamaps>

#if defined datamaps_included
stock CustomDatamap register_weapon(const char[] classname, const char[] name, WeaponInfo info, const char[] based, const char[] clientnet = NULL_STRING)
{
	char netname[64];
	netname[0] = 'C';
	strcopy(netname[1], sizeof(netname)-1, name);

	char dtname[64];
	strcopy(dtname, sizeof(dtname), "DT_");
	strcopy(dtname[3], sizeof(dtname)-3, name);

	CustomEntityFactory factory = EntityFactoryDictionary.register_based_name(classname, based);
	CustomDatamap datamap = CustomDatamap.from_factory(factory);
	datamap.set_name(netname);

	CustomSendtable sendtable = CustomSendtable.from_factory(factory, NULL_STRING);
	if(!IsNullString(clientnet) && clientnet[0] != '\0') {
		sendtable.override_with(clientnet);
	}
	sendtable.set_network_name(netname);
	sendtable.set_name(dtname);

	KeyValues kv = info.__kv;
	precache_weapon_kv(kv, classname, true);
	delete info;

	return datamap;
}
#endif

enum AmmoTracer_t
{
	TRACER_NONE,
	TRACER_LINE,
	TRACER_RAIL,
	TRACER_BEAM,
	TRACER_LINE_AND_WHIZ,
};

enum AmmoFlags_t
{
	AMMO_NOFLAGS = 0,
	AMMO_FORCE_DROP_IF_CARRIED = 0x1,
	AMMO_INTERPRET_PLRDAMAGE_AS_DAMAGE_TO_PLAYER = 0x2,
};

#define INFINITE_AMMO -2

#define POUNDS_PER_KG (2.2)
#define KG_PER_POUND (1.0/POUNDS_PER_KG)
#define lbs2kg(%1) ((%1)*KG_PER_POUND)

#define BULLET_MASS_GRAINS_TO_LB(%1) (0.002285*(%1)/16.0)
#define BULLET_MASS_GRAINS_TO_KG(%1) lbs2kg(BULLET_MASS_GRAINS_TO_LB(%1))

#define BULLET_IMPULSE_EXAGGERATION 1.0

#define BULLET_IMPULSE(%1,%2) ((%2)*12.0*BULLET_MASS_GRAINS_TO_KG(%1)*BULLET_IMPULSE_EXAGGERATION)

native int get_max_ammo_count();
native int get_ammo_count();
native int get_ammo_index(const char[] name);

native Handle add_ammo_type(const char[] name, int dmg_type, AmmoTracer_t tracer, int plr_dmg, int npc_dmg, int carry, float phys_impulse, AmmoFlags_t flags, int min_splash = 4, int max_splash = 8);
native Handle add_ammo_type_cvar(const char[] name, int dmg_type, AmmoTracer_t tracer, const char[] plr_dmg_cvar, const char[] npc_dmg_cvar, const char[] carry_cvar, float phys_impulse, AmmoFlags_t flags, int min_splash = 4, int max_splash = 8);

#if !defined REQUIRE_EXTENSIONS
public void __ext_wpnhack_SetNTVOptional()
{
	
}
#endif

public Extension __ext_wpnhack = 
{
	name = "wpnhack", 
	file = "wpnhack.ext", 
	
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif

#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};
