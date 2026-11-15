using System;
using Summarizer.Core;
using Summarizer.Diagnostics;
using UnityEngine;

namespace Summarizer.App
{
    /// <summary>
    /// Owns the single core context for the app and pumps its events once per frame.
    ///
    /// Everything else talks to the core through <see cref="Context"/> and listens to
    /// <see cref="EventReceived"/>. Events are always raised on the Unity main thread,
    /// so handlers may touch UI directly.
    /// </summary>
    [DefaultExecutionOrder(-1000)]
    public sealed class CoreHost : MonoBehaviour
    {
        // Upper bound on events handled per frame, so a flood of events can never freeze the UI.
        // At 60 fps this is up to 240,000 events per second; anything beyond waits for the next frame.
        private const int MaxEventsPerPoll = 1000;
        private const int MaxPollsPerFrame = 4;

        public static CoreHost Instance { get; private set; }

        /// <summary>Result of loading the core. Check <see cref="CoreCheckResult.IsOk"/> before using <see cref="Context"/>.</summary>
        public CoreCheckResult Check { get; private set; }

        /// <summary>The core context, or null if the core could not be loaded.</summary>
        public CoreContext Context { get; private set; }

        /// <summary>Raised on the main thread for every event, in order.</summary>
        public event Action<CoreEvent> EventReceived;

        /// <summary>Total progress events the core dropped because the queue was full.</summary>
        public long DroppedEvents { get; private set; }

        /// <summary>Events still waiting in the core after the last poll.</summary>
        public long PendingEvents { get; private set; }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void Bootstrap()
        {
            var go = new GameObject("Summarizer");
            DontDestroyOnLoad(go);
            go.AddComponent<CoreHost>();
            go.AddComponent<CoreStatusOverlay>();  // temporary diagnostics, removed in step 3d
        }

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }

            Instance = this;
            Check = NativeCore.Check();
            if (!Check.IsOk)
            {
                Debug.LogError("[Summarizer] " + Check.Error);
                return;
            }

            try
            {
                Context = CoreContext.Create();
                Debug.Log($"[Summarizer] Core {Check.Version} loaded. {Check.BuildInfoJson}");
            }
            catch (Exception e) when (e is CoreException || e is EntryPointNotFoundException)
            {
                // EntryPointNotFound means the DLL is older than these scripts: rebuild the core.
                Check = new CoreCheckResult(true, false, Check.Version, Check.AbiVersion, null,
                                            "Could not create the core context (rebuild the core if it is out of date): " +
                                            e.Message);
                Debug.LogError("[Summarizer] " + Check.Error);
            }
        }

        private void Update()
        {
            if (Context == null || Context.IsDisposed)
            {
                return;
            }

            for (int i = 0; i < MaxPollsPerFrame; i++)
            {
                CoreEventBatch batch = Context.PollEvents(MaxEventsPerPoll);
                DroppedEvents += batch.dropped;
                PendingEvents = batch.pending;

                foreach (CoreEvent e in batch.events)
                {
                    try
                    {
                        EventReceived?.Invoke(e);
                    }
                    catch (Exception ex)
                    {
                        // One broken handler must not stop the others or the pump.
                        Debug.LogException(ex);
                    }
                }

                if (batch.pending == 0)
                {
                    break;
                }
            }
        }

        private void OnDestroy()
        {
            // Stops the worker threads. Without this they would keep running inside the
            // Unity editor process after leaving Play mode.
            Context?.Dispose();
            Context = null;
            if (Instance == this)
            {
                Instance = null;
            }
        }
    }
}
