
#!/usr/bin/env bash

set -u

TEMPOS=(60 66 78 84 96 102 108 114 120 126 132 144 150 156 162 168 174)
SIMPLE=(Q Q Q Q EE)
NORMAL=(Q Q EE TRIP SSSS)
COMPLEX=(Q Q EE EE DOT TRIP ESS SSE SSSS)

RANDOM_BYTES=()
RANDOM_INDEX=0
RANDOM_VALUE=0
NOTE_RESULT=""
PHRASE_TOKENS=()

load_random_bytes()
{
	mapfile -t RANDOM_BYTES < <(
		od -An -v -N 1024 -tu1 /dev/urandom |
		tr -s ' ' '\n' |
		sed '/^$/d'
	)
}

next_byte()
{
	if ((RANDOM_INDEX >= ${#RANDOM_BYTES[@]})); then
		load_random_bytes
		RANDOM_INDEX=0
	fi

	RANDOM_VALUE=${RANDOM_BYTES[RANDOM_INDEX]}
	((RANDOM_INDEX += 1))
}

rand_mod()
{
	local limit=$1

	next_byte
	RANDOM_VALUE=$((RANDOM_VALUE % limit))
}

is_yes()
{
	[[ $1 == y ||
	   $1 == Y ||
	   $1 == yes ||
	   $1 == Yes ||
	   $1 == YES ]]
}

random_note()
{
	local lowest=$1
	local highest=$2
	local allow_rests=$3
	local index
	local note
	local octave
	local octave_count

	if is_yes "$allow_rests"; then
		rand_mod 6

		if ((RANDOM_VALUE == 0)); then
			NOTE_RESULT="R"
			return
		fi
	fi

	rand_mod 8
	index=$RANDOM_VALUE

	case $index in
		0)
			note=G
			;;
		1)
			note=A
			;;
		2)
			note=B
			;;
		3)
			note=C
			;;
		4)
			note=D
			;;
		5)
			note=E
			;;
		6)
			note=F
			;;
		7)
			note=G
			;;
	esac

	octave_count=$((highest - lowest + 1))
	rand_mod "$octave_count"
	octave=$((lowest + RANDOM_VALUE))

	NOTE_RESULT="${note}${octave}"
}

append_note()
{
	local lowest=$1
	local highest=$2
	local allow_rests=$3
	local duration=$4

	random_note "$lowest" "$highest" "$allow_rests"
	PHRASE_TOKENS+=("${NOTE_RESULT}${duration}")
}

append_four_sixteenths()
{
	local lowest=$1
	local highest=$2
	local allow_rests=$3
	local first
	local second

	random_note "$lowest" "$highest" "$allow_rests"
	first=$NOTE_RESULT

	random_note "$lowest" "$highest" "$allow_rests"
	second=$NOTE_RESULT

	PHRASE_TOKENS+=(
		"${first}s"
		"${second}s"
		"${first}s"
		"${second}s"
	)
}

generate_phrase()
{
	local lowest=$1
	local highest=$2
	local allow_rests=$3
	local complexity=$4
	local six_eight=$5
	local base_tempo=$6
	local triplet_tempo=$((base_tempo * 3 / 2))
	local dotted_tempo=$((base_tempo * 2 / 3))
	local slots
	local rhythm
	local i
	local -a patterns

	case $complexity in
		1)
			patterns=("${SIMPLE[@]}")
			;;
		2)
			patterns=("${NORMAL[@]}")
			;;
		3)
			patterns=("${COMPLEX[@]}")
			;;
	esac

	if is_yes "$six_eight"; then
		slots=6
	else
		slots=8
	fi

	PHRASE_TOKENS=()

	for ((i = 0; i < slots; i++)); do
		rand_mod "${#patterns[@]}"
		rhythm=${patterns[RANDOM_VALUE]}

		case $rhythm in
			Q)
				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					q
				;;

			EE)
				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					e

				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					e
				;;

			TRIP)
				PHRASE_TOKENS+=("T${triplet_tempo}")

				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					e

				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					e

				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					e

				PHRASE_TOKENS+=("T${base_tempo}")
				;;

			SSSS)
				append_four_sixteenths \
					"$lowest" \
					"$highest" \
					"$allow_rests"
				;;

			DOT)
				PHRASE_TOKENS+=("T${dotted_tempo}")

				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					e

				PHRASE_TOKENS+=("T${base_tempo}")

				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					s
				;;

			ESS)
				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					e

				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					s

				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					s
				;;

			SSE)
				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					s

				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					s

				append_note \
					"$lowest" \
					"$highest" \
					"$allow_rests" \
					e
				;;
		esac
	done
}

print_sixteen_parts()
{
	local -a tokens=("$@")
	local total_notes=0
	local token
	local base_count
	local remainder
	local section
	local target
	local note_count
	local index=0
	local active_tempo
	local -a part

	for token in "${tokens[@]}"; do
		if [[ $token != T* ]]; then
			((total_notes += 1))
		fi
	done

	base_count=$((total_notes / 16))
	remainder=$((total_notes % 16))
	active_tempo=${tokens[0]#T}
	index=1

	for ((section = 0; section < 16; section++)); do
		target=$base_count

		if ((section < remainder)); then
			((target += 1))
		fi

		while ((index < ${#tokens[@]})) &&
		      [[ ${tokens[index]} == T* ]]; do
			active_tempo=${tokens[index]#T}
			((index += 1))
		done

		part=("T${active_tempo}")
		note_count=0

		while ((index < ${#tokens[@]} &&
		       note_count < target)); do
			token=${tokens[index]}
			((index += 1))

			if [[ $token == T* ]]; then
				active_tempo=${token#T}

				if [[ ${part[${#part[@]}-1]} != "$token" ]]; then
					part+=("$token")
				fi
			else
				part+=("$token")
				((note_count += 1))
			fi
		done

		printf 'tune("%s");\n' "${part[*]}"
	done
}

print_lyrics()
{
	local -a tokens=("$@")
	local active_tempo
	local token
	local index

	active_tempo=${tokens[0]#T}

	for ((index = 1; index < ${#tokens[@]}; index++)); do
		token=${tokens[index]}

		if [[ $token == T* ]]; then
			active_tempo=${token#T}
		else
			printf 'tune("T%s %s");\n' \
				"$active_tempo" \
				"$token"
		fi
	done
}

load_random_bytes

read -rp "Octave range (example: 2-7, Enter=random): " octave_range
read -rp "Allow rests? (y/N): " allow_rests
read -rp "Complexity (1, 2, 3) [2]: " complexity
read -rp "Use 6/8 time? (y/N): " six_eight
read -rp "Lyrics? (y/N): " lyrics

if [[ -z $octave_range ]]; then
	rand_mod 10
	lowest_octave=$RANDOM_VALUE

	rand_mod $((10 - lowest_octave))
	highest_octave=$((lowest_octave + RANDOM_VALUE))
elif [[ $octave_range =~ ^([0-9]+)-([0-9]+)$ ]]; then
	lowest_octave=${BASH_REMATCH[1]}
	highest_octave=${BASH_REMATCH[2]}
elif [[ $octave_range =~ ^[0-9]+$ ]]; then
	lowest_octave=$octave_range
	highest_octave=$octave_range
else
	echo "Error: Enter one octave or a range such as 2-7." >&2
	exit 1
fi

if ((lowest_octave > highest_octave)); then
	echo "Error: Lowest octave cannot be higher than highest octave." >&2
	exit 1
fi

if [[ -z $allow_rests ]]; then
	allow_rests=n
fi

if [[ -z $complexity ]]; then
	complexity=2
fi

if [[ -z $six_eight ]]; then
	six_eight=n
fi

if [[ -z $lyrics ]]; then
	lyrics=n
fi

if ! [[ $allow_rests =~ ^([Yy]|[Yy][Ee][Ss]|[Nn]|[Nn][Oo])$ ]]; then
	echo "Error: Enter y or n for rests." >&2
	exit 1
fi

if ! [[ $complexity =~ ^[123]$ ]]; then
	echo "Error: Complexity must be 1, 2, or 3." >&2
	exit 1
fi

if ! [[ $six_eight =~ ^([Yy]|[Yy][Ee][Ss]|[Nn]|[Nn][Oo])$ ]]; then
	echo "Error: Enter y or n for 6/8 time." >&2
	exit 1
fi

if ! [[ $lyrics =~ ^([Yy]|[Yy][Ee][Ss]|[Nn]|[Nn][Oo])$ ]]; then
	echo "Error: Enter y or n for lyrics." >&2
	exit 1
fi

rand_mod "${#TEMPOS[@]}"
tempo=${TEMPOS[RANDOM_VALUE]}

generate_phrase \
	"$lowest_octave" \
	"$highest_octave" \
	"$allow_rests" \
	"$complexity" \
	"$six_eight" \
	"$tempo"

phrase_a=("${PHRASE_TOKENS[@]}")

generate_phrase \
	"$lowest_octave" \
	"$highest_octave" \
	"$allow_rests" \
	"$complexity" \
	"$six_eight" \
	"$tempo"

phrase_b=("${PHRASE_TOKENS[@]}")

song_tokens=(
	"T${tempo}"
	"${phrase_a[@]}"
	"${phrase_a[@]}"
	"${phrase_b[@]}"
	"${phrase_b[@]}"
)

if is_yes "$lyrics"; then
	print_lyrics "${song_tokens[@]}"
	echo " "
	print_lyrics "${song_tokens[@]}"
else
	print_sixteen_parts "${song_tokens[@]}"
	echo " "
        print_sixteen_parts "${song_tokens[@]}"
fi

