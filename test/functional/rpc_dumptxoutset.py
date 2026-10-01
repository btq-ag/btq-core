#!/usr/bin/env python3
# Copyright (c) 2019-2022 The BTQ Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the generation of UTXO snapshots using `dumptxoutset`.
"""

from test_framework.blocktools import COINBASE_MATURITY
from test_framework.test_framework import BTQTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    sha256sum_file,
)


class DumptxoutsetTest(BTQTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        """Test a trivial usage of the dumptxoutset RPC command."""
        node = self.nodes[0]
        mocktime = node.getblockheader(node.getblockhash(0))['time'] + 1
        node.setmocktime(mocktime)
        self.generate(node, COINBASE_MATURITY)

        FILENAME = 'txoutset.dat'
        out = node.dumptxoutset(FILENAME)
        expected_path = node.datadir_path / self.chain / FILENAME

        assert expected_path.is_file()

        assert_equal(out['coins_written'], 100)
        assert_equal(out['base_height'], 100)
        assert_equal(out['path'], str(expected_path))
        # BTQ vectors use the current genesis and 5-BTQ block subsidy.
        # Blockhash should be deterministic based on mocked time.
        assert_equal(
            out['base_hash'],
            '3330f99f9ec7f7d31f160b168ac7963957a5347ad772f90cb5b2fb848a5f3726')

        # UTXO snapshot hash should be deterministic based on mocked time.
        assert_equal(
            sha256sum_file(str(expected_path)).hex(),
            '4da11bd129791a49bca85ec3fb5a375f20698e6ad18d57da92f90207a505ab8e')

        assert_equal(
            out['txoutset_hash'], '80356d60b0e1c13eefd5e0bbc52fced2a9915d1445b6c714690ea126c49294f8')
        assert_equal(out['nchaintx'], 101)

        # Specifying a path to an existing or invalid file will fail.
        assert_raises_rpc_error(
            -8, '{} already exists'.format(FILENAME),  node.dumptxoutset, FILENAME)
        invalid_path = node.datadir_path / "invalid" / "path"
        assert_raises_rpc_error(
            -8, "Couldn't open file {}.incomplete for writing".format(invalid_path), node.dumptxoutset, invalid_path)


if __name__ == '__main__':
    DumptxoutsetTest().main()
