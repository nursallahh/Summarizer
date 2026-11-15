using System;
using Summarizer.App;
using Summarizer.Core;
using UnityEngine;

namespace Summarizer.Diagnostics
{
    /// <summary>
    /// TEMPORARY (phase 0): shows whether the C++ core loaded and runs the job/event stress test.
    /// Created by <see cref="CoreHost"/>. Replaced by the real UI Toolkit screens in step 3d.
    /// </summary>
    public sealed class CoreStatusOverlay : MonoBehaviour
    {
        // Stress test from the phase 0 plan: 1,000 events per second for 10 seconds.
        private const int StressSteps = 10_000;
        private const int StressStepsPerSecond = 1_000;

        private CoreHost _host;
        private GUIStyle _title;
        private GUIStyle _body;

        private long _jobId;
        private string _jobState = "not started";
        private long _done;
        private long _total;
        private long _eventsReceived;

        private long _eventsThisSecond;
        private float _secondStartedAt;
        private long _eventsPerSecond;
        private float _smoothedFrameMs;
        private float _worstFrameMs;

        private void Start()
        {
            _host = CoreHost.Instance;
            if (_host != null)
            {
                _host.EventReceived += OnCoreEvent;
            }
        }

        private void OnDestroy()
        {
            if (_host != null)
            {
                _host.EventReceived -= OnCoreEvent;
            }
        }

        private void OnCoreEvent(CoreEvent e)
        {
            _eventsReceived++;
            _eventsThisSecond++;
            if (e.job != _jobId)
            {
                return;
            }

            if (e.IsStarted)
            {
                _jobState = "running";
            }
            else if (e.IsProgress)
            {
                _done = e.done;
                _total = e.total;
            }
            else if (e.IsFinished)
            {
                _jobState = e.outcome + (string.IsNullOrEmpty(e.message) ? "" : ": " + e.message);
            }
        }

        private void Update()
        {
            float frameMs = Time.unscaledDeltaTime * 1000f;
            _smoothedFrameMs = Mathf.Lerp(_smoothedFrameMs, frameMs, 0.1f);
            if (_jobState == "running")
            {
                _worstFrameMs = Mathf.Max(_worstFrameMs, frameMs);
            }

            if (Time.unscaledTime - _secondStartedAt >= 1f)
            {
                _eventsPerSecond = _eventsThisSecond;
                _eventsThisSecond = 0;
                _secondStartedAt = Time.unscaledTime;
            }
        }

        private void OnGUI()
        {
            // GUI.skin is only available inside OnGUI, so styles are created here once.
            _title ??= new GUIStyle(GUI.skin.label) { fontSize = 26, fontStyle = FontStyle.Bold };
            _body ??= new GUIStyle(GUI.skin.label) { fontSize = 20, wordWrap = true };

            GUILayout.BeginArea(new Rect(32, 32, Screen.width - 64, Screen.height - 64));

            if (_host == null || !_host.Check.IsOk)
            {
                _title.normal.textColor = new Color(0.95f, 0.35f, 0.3f);
                GUILayout.Label("C++ core could not be used", _title);
                GUILayout.Label(_host == null ? "CoreHost is missing." : _host.Check.Error, _body);
                GUILayout.EndArea();
                return;
            }

            _title.normal.textColor = new Color(0.2f, 0.75f, 0.35f);
            GUILayout.Label($"C++ core loaded — version {_host.Check.Version}, ABI {_host.Check.AbiVersion}", _title);
            GUILayout.Space(16);

            GUILayout.Label("Job and event stress test: 10,000 progress events at 1,000 per second", _body);
            GUILayout.BeginHorizontal();
            bool running = _jobState == "running" || _jobState == "starting";
            GUI.enabled = !running;
            if (GUILayout.Button("Start stress test", GUILayout.Width(240), GUILayout.Height(40)))
            {
                StartStressTest();
            }

            GUI.enabled = running;
            if (GUILayout.Button("Cancel", GUILayout.Width(140), GUILayout.Height(40)))
            {
                _host.Context.CancelJob(_jobId);
            }

            GUI.enabled = true;
            GUILayout.EndHorizontal();
            GUILayout.Space(8);

            float percent = _total > 0 ? 100f * _done / _total : 0f;
            GUILayout.Label(
                $"Job {_jobId}: {_jobState}\n" +
                $"Progress: {_done:N0} / {_total:N0} ({percent:0.0}%)\n" +
                $"Events received: {_eventsReceived:N0}   (last second: {_eventsPerSecond:N0}/s)\n" +
                $"Dropped by core: {_host.DroppedEvents:N0}   Pending in core: {_host.PendingEvents:N0}\n" +
                $"Frame time: {_smoothedFrameMs:0.0} ms   (worst while running: {_worstFrameMs:0.0} ms)\n" +
                $"Managed memory: {GC.GetTotalMemory(false) / (1024f * 1024f):0.0} MB",
                _body);

            GUILayout.EndArea();
        }

        private void StartStressTest()
        {
            _done = 0;
            _total = StressSteps;
            _worstFrameMs = 0f;
            _jobState = "starting";
            _jobId = _host.Context.StartDemoJob(StressSteps, StressStepsPerSecond);
        }
    }
}
