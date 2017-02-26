
export
.PHONY: build09 buildSv buildall

buildall: build09 buildSv
build09:
	@echo ============================================
	@echo ">>>>>>>>>>>>>>>>>>>start building 09<<<<<<<<<<<<<<<<<<<<<"
	@echo ============================================
	${MAKE} -f Makefile09 -f MakefileCpp
buildSv:
	@echo ============================================
	@echo ">>>>>>>>>>>>>>>>>>>start building ZP<<<<<<<<<<<<<<<<<<<<<"
	@echo ============================================
	${MAKE} -f MakefileServ -f MakefileCpp

