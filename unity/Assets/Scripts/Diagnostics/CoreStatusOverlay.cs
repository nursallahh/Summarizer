using Summarizer.Core;
using UnityEngine;

namespace Summarizer.Diagnostics
{
    /// <summary>
    /// TEMPORARY (phase 0, step 1): shows on screen whether the C++ core was loaded.
    /// It creates itself when Play starts, so no scene changes are needed.
    /// It will be removed when the real UI Toolkit screens arrive in step 3.
    /// </summary>
    public sealed class CoreStatusOverlay : MonoBehaviour
    {
        private CoreCheckResult _result;
        private string _text;
        private GUIStyle _style;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void Bootstrap()
        {
            var go = new GameObject(nameof(CoreStatusOverlay));
            DontDestroyOnLoad(go);
            go.AddComponent<CoreStatusOverlay>();
        }

        private void Awake()
        {
            _result = NativeCore.Check();

            if (_result.IsOk)
            {
                _text = "C++ core loaded\n\n" +
                        $"Version: {_result.Version}\n" +
                        $"C interface (ABI): {_result.AbiVersion} (expected {NativeCore.ExpectedAbiVersion})\n\n" +
                        _result.BuildInfoJson;
                Debug.Log($"[Summarizer] Core {_result.Version} loaded. {_result.BuildInfoJson}");
            }
            else
            {
                _text = "C++ core could not be used\n\n" + _result.Error;
                Debug.LogError("[Summarizer] " + _result.Error);
            }
        }

        private void OnGUI()
        {
            // GUI.skin is only available inside OnGUI, so the style is created here once.
            _style ??= new GUIStyle(GUI.skin.label)
            {
                fontSize = 24,
                wordWrap = true,
                normal = { textColor = _result.IsOk ? new Color(0.2f, 0.75f, 0.35f) : new Color(0.95f, 0.35f, 0.3f) }
            };

            GUILayout.BeginArea(new Rect(32, 32, Screen.width - 64, Screen.height - 64));
            GUILayout.Label(_text, _style);
            GUILayout.EndArea();
        }
    }
}
