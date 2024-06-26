#include "kaspersky.hpp"
#include "hooks.hpp"
#include "utils.hpp"

//
// DriverUnload routine.
//
DRIVER_UNLOAD DriverUnload;

//
// This is just a demo, don't hardcode SSDT indexes
//
constexpr unsigned short NtCreateFile_index = 0x0055;

//
// Driver initialization routine.
//
EXTERN_C NTSTATUS DriverEntry( PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath )
{
	UNREFERENCED_PARAMETER( RegistryPath );

	//
	// Setup DriverUnload routine so this driver can unload safely.
	//
	DriverObject->DriverUnload = &DriverUnload;

	//
	// Initialize global pointers that are used by this driver.
	//
	if ( !utils::init( ) )
	{
		log( "Failed to initialize kernel pointers!" );
		return STATUS_ORDINAL_NOT_FOUND;
	}

	//
	// klhk-related initialization.
	//
	if ( !kaspersky::is_klhk_loaded( ) || !kaspersky::initialize( ) )
	{
		log( "Failed to initialize klhk.sys data!" );
		return STATUS_NOT_FOUND;
	}

	//
	// Initialize the hypervisor.
	//
	const auto status = kaspersky::hvm_init( );

	//
	// Failed to initialize the hypervisor.
	//
	if ( !NT_SUCCESS( status ) )
	{
		log( "hvm_init failed! Status code: 0x%X", status );
		return status;
	}

	//
	// Initialize SSDT hooks.
	//
	const auto success = kaspersky::hook_ssdt_routine( NtCreateFile_index, &hooks::hk_NtCreateFile,
													   reinterpret_cast< void** >( &o_NtCreateFile ) );

	return success ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

//
// Driver cleanup. Undo hooks.
//
void DriverUnload( PDRIVER_OBJECT DriverObject )
{
	UNREFERENCED_PARAMETER( DriverObject );

	//
	// Unhook NtCreateFile if klhk.sys is still loaded.
	//
	if ( !kaspersky::is_klhk_loaded( ) )
		return;

	//
	// Undo hooks.
	//
	if ( !kaspersky::unhook_ssdt_routine( NtCreateFile_index, o_NtCreateFile ) )
		log( "Failed to unhook NtCreateFile. Probably not hooked." );

	//
	// Delay execution to make sure no thread is executing our hook. (Better solution: implement a synchronization mechanism).
	//
	LARGE_INTEGER LargeInteger { };
	LargeInteger.QuadPart = -10000000;

	KeDelayExecutionThread( KernelMode, FALSE, &LargeInteger );
}