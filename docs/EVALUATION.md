# Evaluation data

How to measure how well Summarizer sorts real course files into courses.

The model suggests a course for every file; the person only confirms or corrects the suggestions
on a review page. Accuracy is then the share of suggestions the person did not have to change.

## Rules

- **Your course files stay where they are.** The commands only read them. They never move, rename,
  copy or change a file.
- **The table never enters the repository.** It contains personal course data. The commands refuse
  to write it inside a git repository or inside the scanned folder.
- **Labels are tied to file content, not file names.** A file keeps its label after you rename or move it.
- **The API key stays on your computer**, in the `GEMINI_API_KEY` environment variable. It is sent only
  to Google, in a request header, and never written to the table or the logs.
- On the free tier Google may use what is sent to improve its products. Do not classify files with
  personal information (names with grades, ID cards, faces).

## 1. Course list

Create `courses.txt` next to the table (by default: next to the folder you scan). One course per line;
a short description after `|` helps the model tell similar courses apart:

```
Discrete Mathematics | logic, sets, relations, graphs, counting
Differential Equations | first and second order ODEs, Laplace transform
```

## 2. Suggestions

```powershell
.\build\windows-release\bin\Release\summarizer.exe classify "C:\Users\<you>\Desktop\dersnotlari"
```

This scans the folder, asks Gemini about every file that has no suggestion yet, and writes:

- `dersnotlari-scan.csv`: the table, saved after every file, so nothing is lost if the run stops
- `dersnotlari-scan-review.html`: the review page

Photos, PDFs, PowerPoint, Word and text files are supported. When the free per-minute limit is reached
the command waits; when the daily limit is reached it stops, and the same command continues the next day.

Options: `--model <id>` (default `gemini-3.5-flash-lite`), `--rpm <n>` requests per minute (default 10),
`--redo` to ask again for files that already have a suggestion.

## 3. Review

Open the review page in Chrome or Edge. Files are grouped by suggested course. In each group, change the
course of any file that is wrong, then press **Confirm group**. Use the **Model unsure** filter to check
the doubtful ones first. Press **Save reviewed table** and save it over `dersnotlari-scan.csv`.

Only confirmed or changed files get a course; anything you skip stays unreviewed and is not counted.

## 4. Report

```powershell
.\build\windows-release\bin\Release\summarizer.exe report "C:\Users\<you>\Desktop\dersnotlari-scan.csv"
```

Prints accuracy with a 95% range, by file type, by course and by the model's own confidence, how many
files could be accepted automatically, and every wrong suggestion. Files the model could not classify
count as wrong.

## Adding files later

Add new files to the same folder and run `classify` again. Courses, notes and suggestions already in the
table are kept, only new files are sent, and the previous table is saved as `.bak`. Close the table in
Excel before running a command, otherwise Windows does not let it be replaced.

`scan` does the same without classification.
