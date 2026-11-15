using System;
using Microsoft.Win32.SafeHandles;
using UnityEngine;

namespace Summarizer.Core
{
    /// <summary>
    /// Owns a native smz_context. SafeHandle guarantees smz_destroy runs exactly once,
    /// even if Dispose is forgotten, and never while a native call is still using the handle.
    /// </summary>
    internal sealed class CoreContextHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        public CoreContextHandle() : base(ownsHandle: true)
        {
        }

        protected override bool ReleaseHandle()
        {
            NativeCore.Native.smz_destroy(handle);
            return true;
        }
    }

    /// <summary>One event from the core. Fields not used by an event type keep their defaults.</summary>
    [Serializable]
    public sealed class CoreEvent
    {
        public string type;     // "job_started", "job_progress", "job_finished"
        public long job;
        public string kind;     // job_started
        public long done;       // job_progress
        public long total;      // job_progress
        public string outcome;  // job_finished: "succeeded", "cancelled", "failed"
        public string message;  // job_finished

        // file_imported
        public string path;
        public string name;
        public string sha256;
        public string file_type;   // jpeg, png, heic, webp, pdf, pptx, docx, text, audio, unsupported
        public long size;
        public int pages = -1;     // slides for pptx; -1 when not known yet
        public string captured_at; // photo capture time from EXIF, or empty
        public string error;

        public bool IsStarted => type == "job_started";
        public bool IsProgress => type == "job_progress";
        public bool IsFinished => type == "job_finished";
        public bool IsFileImported => type == "file_imported";
    }

    /// <summary>The result of one smz_poll_events call.</summary>
    [Serializable]
    public sealed class CoreEventBatch
    {
        public CoreEvent[] events = Array.Empty<CoreEvent>();
        public long dropped;
        public long pending;
    }

    /// <summary>
    /// C# side of a core context: starts jobs and drains their events.
    /// Create one per app (CoreHost does this) and dispose it on shutdown.
    /// </summary>
    public sealed class CoreContext : IDisposable
    {
        private readonly CoreContextHandle _handle;

        private CoreContext(CoreContextHandle handle)
        {
            _handle = handle;
        }

        public static CoreContext Create()
        {
            // The marshaller fills the SafeHandle directly, so the native pointer is never
            // held in a plain IntPtr that could leak if something threw in between.
            int status = NativeCore.Native.smz_create(out CoreContextHandle handle);
            if (status != NativeCore.StatusOk)
            {
                handle.Dispose();
                NativeCore.ThrowIfFailed(status);
            }

            return new CoreContext(handle);
        }

        /// <summary>Removes up to <paramref name="maxEvents"/> events from the core's queue.</summary>
        public CoreEventBatch PollEvents(int maxEvents)
        {
            ThrowIfDisposed();
            int status = NativeCore.Native.smz_poll_events(_handle, maxEvents, out IntPtr json);
            NativeCore.ThrowIfFailed(status);
            return JsonUtility.FromJson<CoreEventBatch>(NativeCore.TakeString(json));
        }

        /// <summary>Starts the demo job used to test the job/event pipeline. Returns the job id.</summary>
        public long StartDemoJob(int totalSteps, int stepsPerSecond)
        {
            ThrowIfDisposed();
            int status = NativeCore.Native.smz_start_demo_job(_handle, totalSteps, stepsPerSecond, out long jobId);
            NativeCore.ThrowIfFailed(status);
            return jobId;
        }

        /// <summary>
        /// Starts inspecting files and folders (read-only). Emits one file_imported event per file.
        /// Returns the job id.
        /// </summary>
        public long ImportPaths(System.Collections.Generic.IEnumerable<string> paths, bool recursive)
        {
            ThrowIfDisposed();
            string joined = string.Join("\n", paths);
            byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(joined + "\0");
            int status = NativeCore.Native.smz_import_paths(_handle, utf8, recursive ? 1 : 0, out long jobId);
            NativeCore.ThrowIfFailed(status);
            return jobId;
        }

        /// <summary>Requests cancellation. Returns false if the job already finished.</summary>
        public bool CancelJob(long jobId)
        {
            ThrowIfDisposed();
            int status = NativeCore.Native.smz_cancel_job(_handle, jobId);
            if (status == NativeCore.StatusNotFound)
            {
                return false;
            }

            NativeCore.ThrowIfFailed(status);
            return true;
        }

        public bool IsDisposed => _handle.IsClosed;

        /// <summary>Cancels all jobs and frees the native context. Safe to call more than once.</summary>
        public void Dispose()
        {
            _handle.Dispose();
        }

        private void ThrowIfDisposed()
        {
            if (_handle.IsClosed)
            {
                throw new ObjectDisposedException(nameof(CoreContext));
            }
        }
    }
}
