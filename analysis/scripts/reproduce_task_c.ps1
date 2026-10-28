# PowerShell 7, Node.js 24 and Python 3.10; run from OpenBGI project root.
# Reproduce task C's static artifacts; game resources are read only and outputs stay under analysis/out.
param([string]$GameDir = 'E:\Games\Amairo Chocolate 2')
$ErrorActionPreference = 'Stop'
function Check-Exit { if ($LASTEXITCODE -ne 0) { throw "Command failed: $LASTEXITCODE" } }
node analysis/scripts/survey_scripts.js $GameDir analysis/out/script_manifest.tsv
Check-Exit
node analysis/scripts/extract_bp_corpus.js $GameDir analysis/out/bp_corpus_v2
Check-Exit
node analysis/scripts/extract_bs_corpus.js $GameDir analysis/out/bs_corpus
Check-Exit
node analysis/scripts/index_handlers.js (Join-Path $GameDir 'ac2_ML.exe') analysis/out/ac2_ML.c analysis/out/handler_reads.tsv
Check-Exit
py -3 analysis/scripts/index_bs_dispatch.py analysis/out/bp_corpus_v2 analysis/out/bs_dispatch_candidates.tsv
Check-Exit
py -3 analysis/scripts/analyze_bs_reads.py analysis/out/bp_corpus_v2 analysis/out/bs_dispatch_candidates.tsv analysis/out/bs_handler_reads.tsv
Check-Exit
py -3 analysis/scripts/parse_bs.py analysis/out/bs_corpus/index.tsv analysis/out
Check-Exit
py -3 analysis/scripts/bs_audit.py analysis/out/bs_corpus/index.tsv analysis/out
Check-Exit
py -3 analysis/scripts/scan_bp_linear.py analysis/out/bp_corpus_v2 analysis/out/bp_linear_all.tsv
Check-Exit
py -3 analysis/scripts/survey_bp_tables.py analysis/out/bp_corpus_v2 analysis/out/bp_candidate_tables.tsv
Check-Exit
py -3 analysis/scripts/survey_bp_stored_functions.py analysis/out/bp_corpus_v2 analysis/out/bp_candidate_stores.tsv
Check-Exit
py -3 analysis/scripts/merge_bp_candidates.py analysis/out/bp_candidate_tables.tsv analysis/out/bp_candidate_stores.tsv analysis/out/bp_candidate_all.tsv
Check-Exit
py -3 analysis/scripts/audit_bp_functions.py analysis/out/bp_corpus_v2 analysis/out/bp_linear_all.tsv analysis/out/bp_candidate_all.tsv analysis/out/bp_function_candidates.tsv
Check-Exit
py -3 analysis/scripts/audit_bp_all_functions.py analysis/out/bp_corpus_v2 analysis/out/bp_linear_all.tsv analysis/out/bp_functions.tsv
Check-Exit
py -3 analysis/scripts/bp_usage.py analysis/out/bp_corpus_v2 analysis/out/bp_linear_all.tsv analysis/out/subop_usage.tsv
Check-Exit
py -3 analysis/scripts/validate_bs_hypothesis.py analysis/out/bs_corpus/index.tsv analysis/out/bs_dispatch_candidates.tsv analysis/out/bs_handler_reads.tsv analysis/out/bs_boundary_validation.tsv
Check-Exit
foreach ($Language in @('en', 'sc', 'tc')) {
 node analysis/scripts/extract_dsc.js (Join-Path $GameDir 'data06000.arc') "$Language._bsi" "analysis/out/$Language._bsi"
 Check-Exit
 node analysis/scripts/survey_bsi.js "analysis/out/$Language._bsi" "analysis/out/${Language}_bsi_index.tsv"
 Check-Exit
}
py -3 analysis/scripts/audit_91f6.py (Join-Path $GameDir 'ac2_ML.exe') analysis/out/bp_corpus_v2 | Set-Content analysis/out/91f6_audit.txt -Encoding utf8
Check-Exit
