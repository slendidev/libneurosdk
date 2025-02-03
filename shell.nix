{
	pkgs ? import <nixpkgs> { }
}:

pkgs.mkShell {
	buildInputs = with pkgs; [
		clang-tools
		lldb

		curl
		bear
	];
}
