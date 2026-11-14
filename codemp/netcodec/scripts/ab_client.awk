# One run of netcodec_bench_client as the line ab_summary.awk reads (the A/B scripts with BENCH=client):
#   warm encode <ns> (legacy <ns>) decode <ns> (legacy <ns>) mismatches <n> | cold encode <ns> decode <ns>
# in nanoseconds per client packet; mismatches: the netcodec round-trip failures.
/^legacy coding time/ {
	gsub(/[(),]/, " ")
	for (k = 1; k <= NF; k++) {
		if ($k == "encode") lce = $(k + 1)
		if ($k == "decode") lcd = $(k + 1)
		if ($k == "warm") { if (lwe == "") lwe = $(k + 1); else lwd = $(k + 1) }
	}
}
/^netcodec coding time/ {
	gsub(/[(),]/, " ")
	for (k = 1; k <= NF; k++) {
		if ($k == "encode") ce = $(k + 1)
		if ($k == "decode") cd = $(k + 1)
		if ($k == "warm") { if (we == "") we = $(k + 1); else wd = $(k + 1) }
	}
}
/^netcodec all:/ { for (k = 2; k <= NF; k++) if ($k == "failures") mm = $(k - 1) }
END { printf "warm encode %.1f (legacy %.1f) decode %.1f (legacy %.1f) mismatches %d | cold encode %.0f decode %.0f\n", we, lwe, wd, lwd, mm, ce, cd }
