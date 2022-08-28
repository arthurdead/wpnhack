#define private protected
#include <core/logic/HandleSys.h>
#include <core/logic/ShareSys.h>
#include <core/logic/PluginSys.h>
#include <core/logic/ExtensionSys.h>

inline HandleType_t TypeParent(HandleType_t type)
{
	return (type & ~HANDLESYS_SUBTYPE_MASK);
}

class HandleSystemHack : public HandleSystem
{
public:
	HandleError ReadTF2ItemsHandle(Handle_t handle, HandleType_t type, const HandleSecurity *pSecurity, void **object)
	{
		return ReadHandle__(handle, type, pSecurity, object);
	}

private:
	HandleError ReadHandle__(Handle_t handle, HandleType_t type, const HandleSecurity *pSecurity, void **object)
	{
		unsigned int index;
		QHandle *pHandle;
		HandleError err;
		IdentityToken_t *ident = pSecurity ? pSecurity->pIdentity : NULL;

		if ((err=GetHandle__(handle, ident, &pHandle, &index)) != HandleError_None)
		{
			return err;
		}

		if (!CheckAccess__(pHandle, HandleAccess_Read, pSecurity))
		{
			return HandleError_Access;
		}

		/* Check the type inheritance */
		if (pHandle->type & HANDLESYS_SUBTYPE_MASK)
		{
			if (pHandle->type != type
				&& (TypeParent(pHandle->type) != TypeParent(type)))
			{
				return HandleError_Type;
			}
		} else if (type) {
			if (pHandle->type != type)
			{
				return HandleError_Type;
			}
		}

		if (object)
		{
			/* if we're a clone, the rules change - object is ONLY in our reference */
			if (pHandle->clone)
			{
				pHandle = &m_Handles[pHandle->clone];
			}
			*object = pHandle->object;
		}

		return HandleError_None;
	}

	HandleError GetHandle__(Handle_t handle,
										IdentityToken_t *ident, 
										QHandle **in_pHandle, 
										unsigned int *in_index,
										bool ignoreFree = false)
	{
		unsigned int serial = (handle >> HANDLESYS_HANDLE_BITS);
		unsigned int index = (handle & HANDLESYS_HANDLE_MASK);

		if (index == 0 || index > m_HandleTail || index > HANDLESYS_MAX_HANDLES)
		{
			return HandleError_Index;
		}

		QHandle *pHandle = &m_Handles[index];

		if (!pHandle->set
			|| (pHandle->set == HandleSet_Freed && !ignoreFree))
		{
			return HandleError_Freed;
		} else if (pHandle->set == HandleSet_Identity
			#if 0
				   && ident != GetIdentRoot()
			#endif
		)
		{
			/* Only IdentityHandle() can read this! */
			return HandleError_Identity;
		}
		if (pHandle->serial != serial)
		{
			return HandleError_Changed;
		}

		*in_pHandle = pHandle;
		*in_index = index;

		return HandleError_None;
	}

	bool CheckAccess__(QHandle *pHandle, HandleAccessRight right, const HandleSecurity *pSecurity)
	{
		QHandleType *pType = &m_Types[pHandle->type];
		unsigned int access;

		if (pHandle->access_special)
		{
			access = pHandle->sec.access[right];
		} else {
			access = pType->hndlSec.access[right];
		}

		/* Check if the type's identity matches */
	#if 0
		if (access & HANDLE_RESTRICT_IDENTITY)
		{
			IdentityToken_t *owner = pType->typeSec.ident;
			if (!owner
				|| (!pSecurity || pSecurity->pIdentity != owner))
			{
				return false;
			}
		}
	#endif

		/* Check if the owner is allowed */
		if (access & HANDLE_RESTRICT_OWNER)
		{
			IdentityToken_t *owner = pHandle->owner;
			if (owner
				&& (!pSecurity || pSecurity->pOwner != owner))
			{
				return false;
			}
		}

		return true;
	}
};