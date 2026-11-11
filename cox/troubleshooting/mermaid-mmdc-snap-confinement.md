
# Troubleshooting: "mmdc" says the input file "doesn't exist" when it plainly does
A short worked record of validating the Mermaid diagrams in the HTTP/2 design doc (docs/insights/features/http2-design.md"). "mmdc" (mermaid-cli) refused to read input files that were right there on disk, with a misleading error. Kept because the symptom actively lies about the cause, and the fix (snap confinement → run from "$HOME") is non-obvious and will recur for anyone validating diagrams or any other snap-packaged CLI against repo files.
## Symptom
Extracted each
mermaid • block to a file under /tmp" and ran the
renderer to check syntax before committing:
```
$ mmdc -i /tmp/diag_1.mmd -o /tmp/diag_1.svg
Input file "/tmp/diag_1.md" doesn't exist
```

But the file was unmistakably present:
```
$ 1s -1 /tmp/diag_1.mmd
-rw-rw-r-- 1 user user 660 Nov 3 07:29 /tmp/diag_1.mmd $ cat /tmp/diag_1.mnd
```

# prints the diagram fine
The error names a file that Is / cat" read without complaint.
#* Diagnosis path (including the dead ends)
### 1. First reaction: the extraction didn't write the files - PARTLY A RED HERRING
The first extraction used awk with a / / fence matcher that also matched the OPENING fence, so some blocks were empty. Fixed by extracting with a non-greedy regex in Python and confirming each file's byte count. The files were now definitely correct on disk - and "mmd STILL said "doesn't exist." So the empty-file bug was real but was not the cause of this error.
suspecting your tale generation and start suspecting the tool's tient of the
filesystem.

### 2. Second guess: relative vs absolute path / working directory - WRONG
Copied the file into the current working directory and used a bare relative name (mmdc -i d1.mmd"). Same "doesn't exist." So it was not a path-resolution issue.
### 3. The decisive clue: WHERE the binary lives
٠٠٠
$ type mndc mmdc is /snap/bin/mmdc
$ readlink-f"$(which mdc)"
/usr/bin/snap
"mmdc is a **snap**. Snaps run under confinement: by default a snap can only read files under the user's "$HOME" (and a few allowed paths), NOT /tmp or arbitrary directories. The "file doesn't exist" message is the snap sandbox denying access and the tool reporting it as absence - the file exists, the snap just cannot see it.
### 4.Contimm the fix
$ cd ~ 8& mkdir -p mmv 8& cp mnd mv/ 8& cd mmv I
$ mmdc -i diag_1.mmd -o diag_1.svg
Generating single mermaid chart
$ Is -1 diag_1.svg
# success
# 20 KB SVG produced
Moving the inputs under $HOME" and running there rendered all three diagrams.
## Root cause
"mmdc is distributed as a
**strictly-confined snap**. Snap confinement scopes
filesystem access to "$HOME" (plus "removable-media" etc. if those interfaces are connected); "/tmp is not visible to the snap's mount namespace. A read of a tmp path fails with "ENOENT-like behavior, surfaced by the CLI as
"Input file ".." doesn't exist.
## Fix / working pattern
-   Stage any inputs for a snap-packaged tool UNDER $HOME, and write outputs there too, then move them where you need them.
-   A reusable validation snippet for the repo's Mermaid blocks:

```bash
#Extract every mermaid block and render it from $HOME (snap can read $HOME).
python3 - "$1" «'PY•
Import re, sys, pathlib blocks = re.findall(r"••mermaid\n(.*?)*", pathlib.Path(sys.argv[1]). read_text(), re.DOTALL)
d = pathlib.Path.home() / "mmv"; d.mkdir(exist_ok=True)
for i, b in enumerate(blocks, 1): (d / f"d{i).mmd") write_text(b)
print(len(blocks), "blocks ->", d)
PY
cd "$HOME/mmv" && for f in d*. mnd; do mmdc -i "$f" -o "$(f%.mmd}.svg" && echo "OK $f"; done
```
- Alternative if confinement is a recurring nuisance: install mermaid-cli via
"npm -g instead of snap (no confinement), or render in CI with the npm package. The repo does not depend on either - diagrams are plain-text Mermaid in Markdown that Github renders server-side; local mmdc is only for pre-commit syntax validation.
## Takeaways

1.  **"File doesn't exist" from a sandboxed tool often means "I'm not allowed to
see it," not "it's absent."** Check "Is / cat" first; if they succeed, suspect the tool's sandbox.
2. *Check how the binary is packaged** ( type, readlink -f "$(which ...)"").
 /snap/bin/... → snap confinement → "$HOME-only filesystem by default. The same applies to flatpak and other sandboxed packagings.
3.  **/tmp* is the wrong scratch space for snap tools.** Stage under $HOME.
4.  *Validate generated diagrams before committing**, but don't let the validator's confinement masquerade as a diagram-syntax error - the three diagrams here were valid all along; the sandbox was the whole problem.
