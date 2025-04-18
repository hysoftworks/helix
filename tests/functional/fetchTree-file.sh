source common.sh

clearStore

cd "$TEST_ROOT"

echo foo > test_input
test_input_hash="$(nix hash path test_input)"

test_fetch_file () {
    nix eval --raw --impure --file - <<EOF
    let
        tree = builtins.fetchTree { type = "file"; url = "file://$PWD/test_input"; };
    in
    assert (tree.narHash == "$test_input_hash");
    tree.outPath
EOF
}

test_substitution () {
    # fetch a URL that will never work, make sure it hits the existing one in
    # the store if it exists
    nix eval --raw --impure --file - <<EOF
    let
        tree = builtins.fetchTree { type = "file"; url = "file:///dev/null"; narHash = "$test_input_hash"; };
    in
    tree.outPath
EOF
}

# Make sure that `http(s)` and `file` flake inputs are properly extracted when
# they should be, and treated as opaque files when they should be
test_file_flake_input () {
    rm -fr "$TEST_ROOT/testFlake";
    mkdir "$TEST_ROOT/testFlake";
    pushd testFlake

    mkdir inputs
    echo foo > inputs/test_input_file
    echo '{ outputs = { self }: { }; }' > inputs/flake.nix
    tar cfa test_input.tar.gz inputs
    cp test_input.tar.gz test_input_no_ext
    input_tarball_hash="$(nix hash path test_input.tar.gz)"
    input_directory_hash="$(nix hash path inputs)"

    cat <<EOF > flake.nix
    {
        inputs.no_ext_default_no_unpack = {
            url = "file://$PWD/test_input_no_ext";
            flake = false;
        };
        inputs.no_ext_explicit_unpack = {
            url = "tarball+file://$PWD/test_input_no_ext";
            flake = false;
        };
        inputs.tarball_default_unpack = {
            url = "file://$PWD/test_input.tar.gz";
            flake = false;
        };
        inputs.tarball_explicit_no_unpack = {
            url = "file+file://$PWD/test_input.tar.gz";
            flake = false;
        };
        inputs.flake_no_ext = {
            url = "file://$PWD/test_input_no_ext";
        };
        outputs = { ... }: {};
    }
EOF

    nix flake update
    nix eval --file - <<EOF
    with (builtins.fromJSON (builtins.readFile ./flake.lock));

    # Non-flake inputs whose extension doesn’t match a known archive format should
    # not be unpacked by default
    assert (nodes.no_ext_default_no_unpack.locked.type == "file");
    assert (nodes.no_ext_default_no_unpack.locked.unpack or false == false);
    assert (nodes.no_ext_default_no_unpack.locked.narHash == "$input_tarball_hash");

    # For backwards compatibility, flake inputs that correspond to the
    # old 'tarball' fetcher should still have their type set to 'tarball'
    assert (nodes.tarball_default_unpack.locked.type == "tarball");
    # Unless explicitely specified, the 'unpack' parameter shouldn’t appear here
    # because that would break older Nix implementation versions
    assert (!nodes.tarball_default_unpack.locked ? unpack);
    assert (nodes.tarball_default_unpack.locked.narHash == "$input_directory_hash");

    # Explicitely passing the unpack parameter should enforce the desired behavior
    assert (nodes.no_ext_explicit_unpack.locked.narHash == nodes.tarball_default_unpack.locked.narHash);
    assert (nodes.tarball_explicit_no_unpack.locked.narHash == nodes.no_ext_default_no_unpack.locked.narHash);

    # Flake inputs should always be tarballs
    assert (nodes.flake_no_ext.locked.type == "tarball");

    true
EOF

    # Test tarball URLs on the command line.
    [[ $(nix flake metadata --json file://$PWD/test_input_no_ext | jq -r .resolved.type) = tarball ]]

    popd

    [[ -z "${NIX_DAEMON_PACKAGE-}" ]] && return 0

    # Ensure that a lockfile generated by the current Nix for tarball inputs
    # can still be read by an older Nix implementation

    cat <<EOF > flake.nix
    {
        inputs.tarball = {
            url = "file://$PWD/test_input.tar.gz";
            flake = false;
        };
        outputs = { self, tarball }: {
            foo = builtins.readFile "\${tarball}/test_input_file";
        };
    }
    nix flake update

    clearStore
    "$NIX_DAEMON_PACKAGE/bin/nix" eval .#foo
EOF
}

test_file_flake_input
fetch_file_path=$(test_fetch_file)
fetch_substitute_path=$(test_substitution)

# Substituting with a narHash should give you the same path as fetching it
# directly, which should give you the same path as throwing it in the store
# with recursive ingestion mode.
# Regression test for https://git.lix.systems/lix-project/lix/issues/750
[[ "$fetch_file_path" == "$fetch_substitute_path" ]]

# n.b. these are done after the first one which directly fetches the file in
# order to make sure that substitution is *not* at play in test_fetch_file
flat_hash_path=$(nix store add-file --name source ./test_input)
recursive_hash_path=$(nix store add-path --name source ./test_input)

# Fetching the file should give you a recursive-hashed path
[[ "$flat_hash_path" != "$recursive_hash_path" ]]
[[ "$fetch_file_path" == "$recursive_hash_path" ]]
