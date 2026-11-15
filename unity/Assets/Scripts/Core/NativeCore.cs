using System;
using System.Runtime.InteropServices;

namespace CourseArchive.Core
{
    /// <summary>
    /// Result of loading the C++ core and checking that its C interface matches what Unity expects.
    /// </summary>
    public readonly struct CoreCheckResult
    {
        public readonly bool IsLoaded;
        public readonly bool IsCompatible;
        public readonly string Version;
        public readonly int AbiVersion;
        public readonly string BuildInfoJson;
        public readonly string Error;

        public bool IsOk => IsLoaded && IsCompatible;

        public CoreCheckResult(bool isLoaded, bool isCompatible, string version, int abiVersion,
                               string buildInfoJson, string error)
        {
            IsLoaded = isLoaded;
            IsCompatible = isCompatible;
            Version = version;
            AbiVersion = abiVersion;
            BuildInfoJson = buildInfoJson;
            Error = error;
        }
    }

    /// <summary>Thrown when a core function returns a status other than DA_OK.</summary>
    public sealed class CoreException : Exception
    {
        public int Status { get; }

        public CoreException(int status, string message) : base(message)
        {
            Status = status;
        }
    }

    /// <summary>
    /// Thin C# layer over the C interface in core/include/da/da_api.h.
    ///
    /// Memory rules (see the contract at the top of da_api.h):
    /// - Functions that return "const char*" return a string owned by the core. They are declared
    ///   here as returning IntPtr on purpose: if they were declared as returning string, the .NET
    ///   marshaller would try to free the core's static memory and crash the editor.
    /// - Functions that hand out text through "char** out_..." allocate memory in the core.
    ///   It is always released with da_free_string, in a finally block.
    /// </summary>
    public static class NativeCore
    {
        /// <summary>Must match DA_ABI_VERSION in da_api.h. Bump both together.</summary>
        public const int ExpectedAbiVersion = 1;

        private const string Library = "da_core";
        private const int StatusOk = 0;

        private static class Native
        {
            [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
            public static extern IntPtr da_version();

            [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
            public static extern int da_abi_version();

            [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
            public static extern IntPtr da_status_message(int status);

            [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
            public static extern int da_build_info(out IntPtr outJson);

            [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
            public static extern void da_free_string(IntPtr str);
        }

        /// <summary>Core version, e.g. "0.0.1".</summary>
        public static string Version => Marshal.PtrToStringUTF8(Native.da_version());

        /// <summary>The C interface version the core was built with.</summary>
        public static int AbiVersion => Native.da_abi_version();

        /// <summary>Human-readable message for a core status code.</summary>
        public static string StatusMessage(int status) => Marshal.PtrToStringUTF8(Native.da_status_message(status));

        /// <summary>Build information as a JSON string.</summary>
        public static string GetBuildInfoJson()
        {
            int status = Native.da_build_info(out IntPtr json);
            if (status != StatusOk)
            {
                throw new CoreException(status, StatusMessage(status));
            }

            try
            {
                return Marshal.PtrToStringUTF8(json);
            }
            finally
            {
                Native.da_free_string(json);
            }
        }

        /// <summary>
        /// Loads the core and checks its C interface version. Never throws:
        /// any problem is reported through <see cref="CoreCheckResult.Error"/>.
        /// </summary>
        public static CoreCheckResult Check()
        {
            try
            {
                int abi = AbiVersion;
                string version = Version;

                if (abi != ExpectedAbiVersion)
                {
                    return new CoreCheckResult(true, false, version, abi, null,
                        $"C interface version mismatch: core has {abi}, Unity expects {ExpectedAbiVersion}. " +
                        "Rebuild the core and update NativeCore.ExpectedAbiVersion together.");
                }

                return new CoreCheckResult(true, true, version, abi, GetBuildInfoJson(), null);
            }
            catch (DllNotFoundException)
            {
                return new CoreCheckResult(false, false, null, 0, null,
                    "da_core.dll was not found. Build the core (see docs/DEVELOPMENT.md); " +
                    "the build copies it into Assets/Plugins/x86_64. Then restart Unity.");
            }
            catch (EntryPointNotFoundException e)
            {
                return new CoreCheckResult(false, false, null, 0, null,
                    "da_core.dll is out of date (missing function: " + e.Message + "). " +
                    "Close Unity, rebuild the core, then reopen Unity.");
            }
            catch (CoreException e)
            {
                return new CoreCheckResult(true, false, null, 0, null, "Core error: " + e.Message);
            }
        }
    }
}
