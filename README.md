# Ders Arşivi (Course Archive)

A free, open-source Windows app that turns scattered course files into an organized archive you can actually study from.

## Purpose

Students' course materials — whiteboard and notebook photos, slides, PDFs and screenshots — end up scattered across the phone gallery, WhatsApp and the Downloads folder. Ders Arşivi sorts these files by course, lets you search their contents, and generates summaries and study questions that always show which file each piece of information came from.

## Usage

1. Add your courses: type them in, or upload an image or PDF of your timetable and confirm the courses found in it.
2. Select the files you want to organize, or drag them onto the app window.
3. The app suggests a course for each file. Review the suggestions on a single screen, fix any mistakes and confirm. Files the app is not sure about are set aside as "Needs review".
4. Search your archive by content, not just by file name.
5. Pick a course, topic or set of files and generate summaries and study questions. Every statement links back to its source file.

The app never deletes or overwrites your files, and every batch action can be undone. Your files stay on your computer.

## Requirements

**To use**
- Windows 10 or 11 (64-bit)
- Optional: a Gemini API key. With a key, the cloud is used for reading handwriting and generating summaries; when the quota runs low or there is no internet connection, the app continues with a local fallback model.
- Optional: an NVIDIA graphics card (speeds up the local fallback model)

> **Note:** On the Gemini API free tier, content you send may be used by Google to improve its products. Use the app's "local only" option for courses that contain sensitive information.

**To develop**
- Visual Studio 2022 or newer ("Desktop development with C++" workload)
- CMake 3.25 or newer
- vcpkg
- Unity (LTS release)

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for build instructions.

## License

This project is licensed under the Apache License 2.0. See the `LICENSE` file for details.
