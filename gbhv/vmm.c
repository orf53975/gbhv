#include "vmm.h"

#include <wdm.h>
#include "vmx.h"

/**
 * Call HvInitializeLogicalProcessor on all processors using an Inter-Process Interrupt (IPI).
 * 
 * All processors will stop executing until all processors have entered VMX root-mode.
 */
BOOL HvInitializeAllProcessors()
{
	SIZE_T FeatureMSR;

	HvUtilLog("HvInitializeAllProcessors: Starting.");

	// Check if VMX support is enabled on the processor.
	if (!ArchIsCPUFeaturePresent(CPUID_VMX_ENABLED_FUNCTION, 
			CPUID_VMX_ENABLED_SUBFUNCTION, 
			CPUID_REGISTER_ECX, 
			CPUID_VMX_ENABLED_BIT)
		)
	{
		HvUtilLogError("VMX feature is not present on this processor.");
		return FALSE;
	}


	// Enable bits in MSR to enable VMXON instruction.
	FeatureMSR = ArchGetHostMSR(MSR_IA32_FEATURE_CONTROL_ADDRESS);

	HvUtilLogDebug("FeatureMSR: %x", FeatureMSR);

	// The BIOS will lock the VMX bit on startup.
	if(!HvUtilBitIsSet(FeatureMSR, FEATURE_BIT_VMX_LOCK))
	{
		HvUtilLogError("VMX support was not locked by BIOS.");
		return FALSE;
	}

	// VMX support can be configured to be disabled outside SMX.
	// Check to ensure this isn't the case.
	if (!HvUtilBitIsSet(FeatureMSR, FEATURE_BIT_ALLOW_VMX_OUTSIDE_SMX))
	{
		HvUtilLogError("VMX support was disabled outside of SMX operation by BIOS.");
		return FALSE;
	}

	HvUtilLogDebug("Total Processor Count: %i", OsGetCPUCount());

	PVMX_PROCESSOR_CONTEXT Context = HvInitializeLogicalProcessor();
	
	HvUtilLogSuccess("HvInitializeAllProcessors: Success.");

	// Generates an IPI that signals all processors to execute the broadcast function.
	//KeIpiGenericCall(HvpIPIBroadcastFunction, 0);

	HvFreeLogicalProcessorContext(Context);
	return TRUE;
}

/**
 * Allocates a logical processor context.
 * 
 * Returns NULL on error.
 */
PVMX_PROCESSOR_CONTEXT HvAllocateLogicalProcessorContext()
{
	PVMX_PROCESSOR_CONTEXT Context;

	/*
	 * Allocate generic memory.
	 */
	Context = (PVMX_PROCESSOR_CONTEXT)OsAllocateNonpagedMemory(sizeof(VMX_PROCESSOR_CONTEXT));
	if(!Context)
	{
		return NULL;
	}

	/*
	 * See VMX_VMXON_NUMBER_PAGES documentation.
	 */
	Context->VmxonRegion = (PVMXON_REGION)OsAllocateContiguousAlignedPages(VMX_VMXON_NUMBER_PAGES);

	return Context;
}

/*
 * Free a logical processor context allocated by HvAllocateLogicalProcessorContext
 */
VOID HvFreeLogicalProcessorContext(PVMX_PROCESSOR_CONTEXT Context)
{
	if(Context)
	{
		OsFreeContiguousAlignedPages(Context->VmxonRegion);
		OsFreeNonpagedMemory(Context);
	}
}

/**
 * Called by HvInitializeAllProcessors to initialize VMX on all processors.
 */
ULONG_PTR HvpIPIBroadcastFunction(_In_ ULONG_PTR Argument)
{
	UNREFERENCED_PARAMETER(Argument);

	// Initialize VMX on this processor
	HvInitializeLogicalProcessor();

	return 0;
}

/**
 * Initialize VMCS and enter VMX root-mode.
 */
PVMX_PROCESSOR_CONTEXT HvInitializeLogicalProcessor()
{
	SIZE_T CurrentProcessorNumber;
	PVMX_PROCESSOR_CONTEXT Context;

	// Allocate our big internal structure that holds information about this
	// logical processor.
	Context = HvAllocateLogicalProcessorContext();

	// Get the current processor we're executing this function on right now
	CurrentProcessorNumber = OsGetCurrentProcessorNumber();

	HvUtilLog("HvInitializeLogicalProcessor: Startup [Processor = #%i, Context = 0x%llx]", CurrentProcessorNumber, Context);

	return Context;
}
