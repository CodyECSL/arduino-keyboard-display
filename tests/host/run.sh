#!/bin/sh
# Build and run the host tests for the game rules (no board needed).
set -e
cd "$(dirname "$0")"
dir="$(mktemp -d)"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    test_game_logic.c ../../src/game_logic.c -lm -o "$dir/test_game_logic"
"$dir/test_game_logic"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    test_crossy_logic.c ../../src/crossy_logic.c -lm -o "$dir/test_crossy_logic"
"$dir/test_crossy_logic"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    test_crossy_ground.c ../../src/crossy_ground.c -lm -o "$dir/test_crossy_ground"
"$dir/test_crossy_ground"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -DCG_SWAP=1 \
    test_crossy_ground.c ../../src/crossy_ground.c -lm -o "$dir/test_crossy_ground_swap"
"$dir/test_crossy_ground_swap"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    test_log_ring.c ../../src/log_ring.c -o "$dir/test_log_ring"
"$dir/test_log_ring"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    test_bmp.c ../../src/bmp.c -o "$dir/test_bmp"
"$dir/test_bmp"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    test_touch_gate.c ../../src/touch_gate.c -o "$dir/test_touch_gate"
"$dir/test_touch_gate"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    test_saver_logic.c ../../src/saver_logic.c -o "$dir/test_saver_logic"
"$dir/test_saver_logic"
