# Evaluation data

How to build the label table that the accuracy tests use.

## Rules

- **Your course files stay where they are.** `summarizer scan` only reads them. It never moves, renames,
  copies or changes a file.
- **The label table never enters the repository.** It contains personal course data. `scan` refuses to
  write it inside a git repository or inside the scanned folder.
- **Labels are tied to file content, not file names.** A file keeps its label after you rename or move it.

## Build or update the table

Run from the repository folder after building (see `DEVELOPMENT.md`):

```powershell
.\build\windows-release\bin\Release\summarizer.exe scan "C:\Users\<you>\Desktop\dersnotlari"
```

This writes `dersnotlari-scan.csv` next to the folder (here on the Desktop). Use
`--out <file.csv>` to choose another place outside the repository.

| Column | Meaning |
| --- | --- |
| `sha256` | Content id. Do not edit. |
| `file_name`, `other_names` | Name of the file; identical copies are merged into one row. |
| `type` | Detected from the content: jpeg, png, heic, webp, pdf, pptx, docx, text, audio |
| `size_kb`, `pages` | Size; slide count for presentations (PDF pages arrive with PDF support) |
| `captured_at` | Photo capture time from the camera. Photos are sorted by it, so photos from the same lecture sit together. |
| `course` | **Fill this in.** One course name per row, written the same way every time. |
| `notes` | Optional. |

Open the file in Excel, fill in `course`, and save. Prefer **File → Save As → CSV UTF-8**.
Plain "CSV" also works; `scan` converts it back from the Windows code page.

## Adding files later

Add new files to the same folder and run the same command again. Courses and notes you already
wrote are kept, new files get new rows, and the previous table is saved as `.bak`. Rows you labelled
whose file has disappeared are kept and marked `[not found in last scan]`.

Close the table in Excel before running `scan`, otherwise Windows does not let it be replaced.
