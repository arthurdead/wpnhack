class CEconItem;

template< class T, class I = int >
class CUtlMemoryTF2Items : public CUtlMemory< T, I >
{
public:
	CUtlMemoryTF2Items( int nGrowSize = 0, int nInitSize = 0 ) { CUtlMemory< T, I >( nGrowSize, nInitSize ); }
    CUtlMemoryTF2Items( T* pMemory, int numElements ) { CUtlMemory< T, I >( pMemory, numElements ); }
    CUtlMemoryTF2Items( const T* pMemory, int numElements ) { CUtlMemory< T, I >( pMemory, numElements ); }
    //~CUtlMemoryTF2Items() { ~CUtlMemory< T, I >(); }
    
	void Purge()
	{
		if ( !CUtlMemory< T, I >::IsExternallyAllocated() )
		{
			if (CUtlMemory< T, I >::m_pMemory)
			{
				UTLMEMORY_TRACK_FREE();
				//free( (void*)m_pMemory );
#ifdef TF2ITEMS_DEBUG_ITEMS
				META_CONPRINTF("CUtlMemory tried to be freed!\n");
#endif
				CUtlMemory< T, I >::m_pMemory = 0;
			}
			CUtlMemory< T, I >::m_nAllocationCount = 0;
		}
	}
};

class CEconItemAttribute
{
public:
	void *m_pVTable; //0

	uint16 m_iAttributeDefinitionIndex; //4
	float m_flValue; //8
	int32 m_nRefundableCurrency; //12
};

#pragma pack(push, 4)


class CEconItemHandle
{
public:
	void *m_pVTable;

	CEconItem *m_pItem;

	int64 m_ulItemID;
	uint64 m_SteamID;
};

class CAttributeList
{
public:
	void *m_pVTable;

	CUtlVector<CEconItemAttribute, CUtlMemoryTF2Items<CEconItemAttribute> > m_Attributes;
	void *m_pAttributeManager;


public:
	CAttributeList& operator=( const CAttributeList &other )
	{
		m_pVTable = other.m_pVTable;

		m_Attributes = other.m_Attributes;
		m_pAttributeManager = other.m_pAttributeManager;


		return *this;
	}
};

class CEconItemView
{
public:
	void *m_pVTable; //0

	uint16 m_iItemDefinitionIndex; //4
	
	int32 m_iEntityQuality; //8
	uint32 m_iEntityLevel; //12

	uint64 m_iItemID; //16
	uint32 m_iItemIDHigh; //24
	uint32 m_iItemIDLow; //28

	uint32 m_iAccountID; //32

	uint32 m_iInventoryPosition; //36
	
	CEconItemHandle m_ItemHandle; //40 (44, 48, 52, 56, 60)

	bool m_bColorInit; //64
	bool m_bPaintOverrideInit; //65
	bool m_bHasPaintOverride; //66
	//67

	float m_flOverrideIndex; //68
	uint32 m_unRGB; //72
	uint32 m_unAltRGB; //76

	int32 m_iTeamNumber; //80

	bool m_bInitialized; //84

	CAttributeList m_AttributeList; //88 (92, 96, 100, 104, 108, 112)
	CAttributeList m_NetworkedDynamicAttributesForDemos; //116 (120, 124, 128, 132, 136, 140)

	bool m_bDoNotIterateStaticAttributes; //144
};

#pragma pack(pop)

static_assert(sizeof(CEconItemView) == 148, "CEconItemView - incorrect size on this compiler");
static_assert(sizeof(CEconItemHandle) == 24, "CEconItemHandle - incorrect size on this compiler");
static_assert(sizeof(CAttributeList) == 28, "CAttributeList - incorrect size on this compiler");

struct TScriptedItemOverride
{
	uint8 m_bFlags;									// Flags to what we should override.
	char m_strWeaponClassname[256];					// Classname to override the GiveNamedItem call with.
	uint32 m_iItemDefinitionIndex;					// New Item Def. Index.
	uint8 m_iEntityQuality;							// New Item Quality Level.
	uint8 m_iEntityLevel;							// New Item Level.
	uint8 m_iCount;									// Count of Attributes.
	CEconItemAttribute m_Attributes[16];			// The actual attributes.
};

HandleType_t g_ScriptedItemOverrideHandleType = 0;

void *g_pVTable;
void *g_pVTable_Attributes;

#define PRESERVE_ATTRIBUTES     (1 << 5)
#define FORCE_GENERATION		(1 << 6)

#define NO_FORCE_QUALITY

#include "funnyfile.h"

TScriptedItemOverride *GetScriptedItemOverrideFromHandle(cell_t cellHandle, IPluginContext *pContext)
{
	Handle_t hndlScriptedItemOverride = static_cast<Handle_t>(cellHandle);
	HandleError hndlError;
	HandleSecurity hndlSec;
 
	// Build our security descriptor
	hndlSec.pOwner = NULL;
	hndlSec.pIdentity = myself->GetIdentity();
 
	// Attempt to read the given handle as our type, using our security info.
	TScriptedItemOverride * pScriptedItemOverride;
	if ((hndlError = ((HandleSystemHack *)g_pHandleSys)->ReadTF2ItemsHandle(hndlScriptedItemOverride, g_ScriptedItemOverrideHandleType, &hndlSec, (void **)&pScriptedItemOverride)) != HandleError_None)
	{
		if (pContext == NULL)
		{
			g_pSM->LogError(myself, "Invalid TF2ItemType handle %x (error %d)", hndlScriptedItemOverride, hndlError);
		} else {
			pContext->ThrowNativeError("Invalid TF2ItemType handle %x (error %d)", hndlScriptedItemOverride, hndlError);
		}

		return NULL;
	}

	return pScriptedItemOverride;
}

const char *init_tf2items_itemview(CEconItemView &hScriptCreatedItem, TScriptedItemOverride *pScriptedItemOverride, bool &bForce)
{
	memset(&hScriptCreatedItem, 0, sizeof(CEconItemView));

	// initialize the vtable pointers
	hScriptCreatedItem.m_pVTable = g_pVTable;
	hScriptCreatedItem.m_AttributeList.m_pVTable = g_pVTable_Attributes;
	hScriptCreatedItem.m_NetworkedDynamicAttributesForDemos.m_pVTable = g_pVTable_Attributes;

	char *strWeaponClassname = pScriptedItemOverride->m_strWeaponClassname;
	hScriptCreatedItem.m_iItemDefinitionIndex = pScriptedItemOverride->m_iItemDefinitionIndex;
	hScriptCreatedItem.m_iEntityLevel = pScriptedItemOverride->m_iEntityLevel;
	hScriptCreatedItem.m_iEntityQuality = pScriptedItemOverride->m_iEntityQuality;
	hScriptCreatedItem.m_AttributeList.m_Attributes.CopyArray(pScriptedItemOverride->m_Attributes, pScriptedItemOverride->m_iCount);
	hScriptCreatedItem.m_bInitialized = true;
	
	if (!(pScriptedItemOverride->m_bFlags & PRESERVE_ATTRIBUTES))
	{
		hScriptCreatedItem.m_bDoNotIterateStaticAttributes = true;
	}

#ifndef NO_FORCE_QUALITY
	if (hScriptCreatedItem.m_iEntityQuality == 0 && hScriptCreatedItem.m_iAttributesCount > 0)
	{
		hScriptCreatedItem.m_iEntityQuality = 6;
	}
#endif

	bForce = ((pScriptedItemOverride->m_bFlags & FORCE_GENERATION) == FORCE_GENERATION);

	return strWeaponClassname;
}

void init_tf2items_vtables(const CEconItemView *cscript)
{
	if (g_pVTable == NULL)
	{
		g_pVTable = cscript->m_pVTable;
		g_pVTable_Attributes = cscript->m_AttributeList.m_pVTable;
	}
}

void init_tf2items_handles()
{
	handlesys->FindHandleType("TF2ItemType", &g_ScriptedItemOverrideHandleType);
}