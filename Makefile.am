# -*- makefile -*-
# $Id: Makefile.am,v 1.5 2004/01/21 04:00:53 eryndil Exp $
# Copyright (C) 2002, Nathan Paul Simons (npsimons@hardcorehackers.com)
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
# Alternatively, the GPL can be found at
# http://www.gnu.org/copyleft/gpl.html


AUTOMAKE_OPTIONS = subdir-objects

SUBDIRS = bin

db-gates:
	@$(top_srcdir)/tools/db_gates.sh

.PHONY: architecture-check
architecture-check:
	@python3 $(top_srcdir)/tools/check_architecture_boundaries.py

.PHONY: regression-static regression-full
regression-static:
	@python3 -m pytest -q \
		tests.v2/test_qa_runner_config.py \
		tests.v2/test_bigbang_sql_selection.py \
		tests.v2/test_server_visibility_contract.py \
		tests.v2/test_ship_transfer_error_contract.py \
		tests.v2/test_issue_490_ship02_contract.py

regression-full:
	@python3 tests.v2/run_regression_instance.py --pg-port "$${QA_PGPORT:-55438}" --report tests.v2/regression_report.json

check-local: architecture-check regression-static
