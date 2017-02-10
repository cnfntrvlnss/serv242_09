
export
.PHONY: build09 buildZP buildall

build09:
	@echo ============================================
	@echo ">>>>>>>>>>>>>>>>>>>start building 09<<<<<<<<<<<<<<<<<<<<<"
	@echo ============================================
	${MAKE} -f Makefile09 -f MakefileCpp
buildZP:
	@echo ============================================
	@echo ">>>>>>>>>>>>>>>>>>>start building ZP<<<<<<<<<<<<<<<<<<<<<"
	@echo ============================================
	${MAKE} -f MakefileZP -f MakefileCpp

buildall: build09 buildZP build242
