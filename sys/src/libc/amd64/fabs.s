TEXT	fabs(SB), $0
	MOVSD	arg+0(FP), X0
	PSLLQ	$(1), X0
	PSRLQ	$(1), X0
	RET
