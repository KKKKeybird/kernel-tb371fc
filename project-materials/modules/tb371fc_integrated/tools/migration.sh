#!/system/bin/sh

set -u

MANAGED_IDS="tb371fc_account_compat tb371fc_split_compat tb371fc_pen_lockfix tb371fc_battery_stats tb371fc_pps_temp_test tb371fc_system_compat tb371fc_kernel_updater tb371fc_stock_charge"

ACTION=
MODULES_DIR=
STATE_DIR=
MODULES_ROOT=
STATE_ROOT=
JOURNAL=
PREPARED=
RUNTIME_READY=
LOCK_DIR=
TMP_FILE=

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

usage() {
    printf '%s\n' \
        'Usage: migration.sh --plan|--snapshot|--disable-prepared|--restore-flags' \
        '                    --modules-dir DIR --state-dir DIR' \
        '' \
        'File-state only. This script does not stop services, touch sysfs, call pm,' \
        'run module uninstall scripts, create remove markers, or flash boot.' \
        '' \
        'Runtime handoff interface (written by the device owner after verified cleanup):' \
        '  $state-dir/runtime-ready.tsv' \
        '  version<TAB>1' \
        '  run<TAB><journal run id>' \
        '  charge<TAB>stopped' \
        '  capacity<TAB>released'
}

cleanup() {
    [ -z "$TMP_FILE" ] || rm -f "$TMP_FILE"
    [ -z "$LOCK_DIR" ] || rmdir "$LOCK_DIR" 2>/dev/null || true
}

trap cleanup EXIT HUP INT TERM

is_managed() {
    case "$1" in
        tb371fc_account_compat|tb371fc_split_compat|tb371fc_pen_lockfix|tb371fc_battery_stats|tb371fc_pps_temp_test|tb371fc_system_compat|tb371fc_kernel_updater|tb371fc_stock_charge)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

path_present() {
    [ -e "$1" ] || [ -L "$1" ]
}

canonical_modules_root() {
    [ -d "$MODULES_DIR" ] || die "modules-dir does not exist: $MODULES_DIR"
    MODULES_ROOT=$(cd -P "$MODULES_DIR" 2>/dev/null && pwd -P) ||
        die "cannot canonicalize modules-dir: $MODULES_DIR"
    case "$MODULES_ROOT" in
        *'
'*) die "modules-dir contains a newline" ;;
        *'	'*) die "modules-dir contains a tab" ;;
    esac
}

canonical_state_root() {
    if [ ! -d "$STATE_DIR" ]; then
        return 1
    fi
    [ ! -L "$STATE_DIR" ] || die "state-dir must not be a symlink: $STATE_DIR"
    STATE_ROOT=$(cd -P "$STATE_DIR" 2>/dev/null && pwd -P) ||
        die "cannot canonicalize state-dir: $STATE_DIR"
    JOURNAL="$STATE_ROOT/journal.tsv"
    PREPARED="$STATE_ROOT/prepared.tsv"
    RUNTIME_READY="$STATE_ROOT/runtime-ready.tsv"
    return 0
}

prepare_state_root() {
    mkdir -p "$STATE_DIR" || die "cannot create state-dir: $STATE_DIR"
    [ ! -L "$STATE_DIR" ] || die "state-dir must not be a symlink: $STATE_DIR"
    STATE_ROOT=$(cd -P "$STATE_DIR" 2>/dev/null && pwd -P) ||
        die "cannot canonicalize state-dir: $STATE_DIR"
    JOURNAL="$STATE_ROOT/journal.tsv"
    PREPARED="$STATE_ROOT/prepared.tsv"
    RUNTIME_READY="$STATE_ROOT/runtime-ready.tsv"
}

acquire_lock() {
    LOCK_DIR="$STATE_ROOT/.migration.lock"
    mkdir "$LOCK_DIR" 2>/dev/null || die "migration lock already exists: $LOCK_DIR"
}

validate_module_dir() {
    id=$1
    module_path="$MODULES_ROOT/$id"

    if ! path_present "$module_path"; then
        return 0
    fi
    [ ! -L "$module_path" ] || return 1
    [ -d "$module_path" ] || return 1

    module_real=$(cd -P "$module_path" 2>/dev/null && pwd -P) || return 1
    [ "${module_real%/*}" = "$MODULES_ROOT" ] || return 1
    [ "${module_real##*/}" = "$id" ] || return 1

    [ -f "$module_path/module.prop" ] || return 1
    [ ! -L "$module_path/module.prop" ] || return 1

    for flag in disable remove skip_mount; do
        flag_path="$module_path/$flag"
        if path_present "$flag_path"; then
            [ ! -L "$flag_path" ] || return 1
            [ -f "$flag_path" ] || return 1
        fi
    done
    return 0
}

validate_all_managed_targets() {
    for id in $MANAGED_IDS; do
        validate_module_dir "$id" ||
            die "unsafe or malformed managed module target: $id"
    done
}

module_state() {
    id=$1
    module_path="$MODULES_ROOT/$id"
    if ! path_present "$module_path"; then
        printf 'missing\n'
    elif path_present "$module_path/remove"; then
        printf 'remove\n'
    elif path_present "$module_path/disable"; then
        printf 'disabled\n'
    else
        printf 'enabled\n'
    fi
}

module_flag() {
    id=$1
    flag=$2
    module_path="$MODULES_ROOT/$id"
    if path_present "$module_path/$flag"; then
        printf '1\n'
    else
        printf '0\n'
    fi
}

validate_prepared() {
    [ -f "$PREPARED" ] || die "prepared manifest is missing: $PREPARED"
    [ ! -L "$PREPARED" ] || die "prepared manifest must not be a symlink"

    parsed=$(
        awk -F '\t' '
            /^[[:space:]]*#/ { next }
            /^[[:space:]]*$/ { next }
            NF != 2 { print "malformed"; exit }
            {
                if ($1 in seen) {
                    print "duplicate:" $1
                    exit
                }
                seen[$1] = 1
                if ($1 !~ /^[A-Za-z0-9_]+$/) {
                    print "invalid-id:" $1
                    exit
                }
            }
        ' "$PREPARED"
    )
    [ -z "$parsed" ] || die "invalid prepared manifest: $parsed"

    while IFS='	' read -r id status; do
        case "$id" in
            ''|'#'*) continue ;;
        esac
        is_managed "$id" || die "unknown component in prepared manifest: $id"
        [ -n "$status" ] || die "missing status for prepared component: $id"
    done < "$PREPARED"
}

prepared_ready() {
    id=$1
    awk -F '\t' -v wanted="$id" '
        $1 == wanted && $2 == "READY" { found = 1 }
        END { exit !found }
    ' "$PREPARED"
}

runtime_ready_matches_journal() {
    [ -f "$RUNTIME_READY" ] || return 1
    [ ! -L "$RUNTIME_READY" ] || return 1

    expected_run=$(journal_header run)
    [ -n "$expected_run" ] || return 1

    bad=$(
        awk -F '\t' '
            /^[[:space:]]*#/ { next }
            /^[[:space:]]*$/ { next }
            NF != 2 { print "malformed"; exit }
            {
                if ($1 in seen) {
                    print "duplicate:" $1
                    exit
                }
                seen[$1] = 1
            }
        ' "$RUNTIME_READY"
    )
    [ -z "$bad" ] || return 1

    [ "$(awk -F '\t' '$1 == "version" { print $2; exit }' "$RUNTIME_READY")" = 1 ] ||
        return 1
    [ "$(awk -F '\t' '$1 == "run" { print $2; exit }' "$RUNTIME_READY")" = "$expected_run" ] ||
        return 1
    [ "$(awk -F '\t' '$1 == "charge" { print $2; exit }' "$RUNTIME_READY")" = stopped ] ||
        return 1
    [ "$(awk -F '\t' '$1 == "capacity" { print $2; exit }' "$RUNTIME_READY")" = released ] ||
        return 1
    return 0
}

print_plan() {
    blocked=0

    canonical_modules_root
    canonical_state_root || die "state-dir does not exist: $STATE_DIR"
    validate_prepared
    validate_all_managed_targets

    for id in $MANAGED_IDS; do
        state=$(module_state "$id")
        ready=no
        prepared_ready "$id" && ready=yes

        case "$state" in
            enabled)
                if [ "$ready" = yes ]; then
                    action=disable
                else
                    action=blocked
                    blocked=1
                fi
                ;;
            disabled)
                action=keep-disabled
                ;;
            remove)
                action=keep-remove
                ;;
            missing)
                action=skip-missing
                ;;
        esac

        printf 'COMPONENT\t%s\tstate=%s\tprepared=%s\taction=%s\n' \
            "$id" "$state" "$ready" "$action"
    done

    for path in "$MODULES_ROOT"/*; do
        [ -d "$path" ] || continue
        name=${path##*/}
        is_managed "$name" && continue
        printf 'IGNORED\t%s\n' "$name"
    done

    if path_present "$JOURNAL" && runtime_ready_matches_journal; then
        printf 'RUNTIME\thandoff-ready\n'
    else
        printf 'RUNTIME\tpending\n'
    fi

    if [ "$blocked" = 0 ]; then
        printf 'RESULT\tplan-ready\n'
        return 0
    fi
    printf 'RESULT\tplan-blocked-missing-ready\n'
    return 1
}

journal_header() {
    key=$1
    awk -F '\t' -v wanted="$key" '$1 == wanted { print $2; exit }' "$JOURNAL"
}

journal_record() {
    id=$1
    column=$2
    awk -F '\t' -v wanted="$id" -v col="$column" '
        $1 == "record" && $2 == wanted { print $col; exit }
    ' "$JOURNAL"
}

journal_has_record() {
    id=$1
    awk -F '\t' -v wanted="$id" '
        $1 == "record" && $2 == wanted { found = 1 }
        END { exit !found }
    ' "$JOURNAL"
}

validate_journal() {
    [ -f "$JOURNAL" ] || die "journal is missing: $JOURNAL"
    [ ! -L "$JOURNAL" ] || die "journal must not be a symlink"

    version=$(journal_header version)
    [ "$version" = 1 ] || die "unsupported journal version: ${version:-missing}"

    saved_root=$(journal_header root)
    [ -n "$saved_root" ] || die "journal root is missing"
    [ "$saved_root" = "$MODULES_ROOT" ] ||
        die "journal modules-dir mismatch: $saved_root != $MODULES_ROOT"

    phase=$(journal_header phase)
    case "$phase" in
        SNAPSHOT|PARTIAL|DISABLED|RESTORE_PARTIAL|RESTORED) ;;
        *) die "invalid journal phase: ${phase:-missing}" ;;
    esac

    bad=$(
        awk -F '\t' '
            $1 == "record" {
                if (NF != 8) {
                    print "field-count:" $2
                    exit
                }
                if ($2 in seen) {
                    print "duplicate:" $2
                    exit
                }
                seen[$2] = 1
            }
        ' "$JOURNAL"
    )
    [ -z "$bad" ] || die "invalid journal record: $bad"

    for id in $MANAGED_IDS; do
        journal_has_record "$id" || die "journal record missing: $id"
    done
}

journal_replace() {
    awk_program=$1
    shift
    TMP_FILE=$(mktemp "$STATE_ROOT/.journal.XXXXXX") ||
        die "cannot create journal temporary file"
    awk -F '\t' -v OFS='\t' "$@" "$awk_program" "$JOURNAL" > "$TMP_FILE" ||
        die "cannot update journal"
    sync
    mv -f "$TMP_FILE" "$JOURNAL" || die "cannot replace journal atomically"
    TMP_FILE=
}

journal_set_phase() {
    value=$1
    journal_replace '
        $1 == "phase" { $2 = value; found = 1 }
        { print }
        END { if (!found) exit 2 }
    ' -v value="$value"
}

journal_mark_record() {
    id=$1
    column=$2
    value=$3
    journal_replace '
        $1 == "record" && $2 == wanted {
            $column = value
            found = 1
        }
        { print }
        END { if (!found) exit 2 }
    ' -v wanted="$id" -v column="$column" -v value="$value"
}

snapshot_plan() {
    canonical_modules_root
    prepare_state_root
    validate_all_managed_targets
    acquire_lock

    if path_present "$JOURNAL"; then
        [ ! -L "$JOURNAL" ] || die "journal must not be a symlink"
        saved_root=$(journal_header root)
        [ "$saved_root" = "$MODULES_ROOT" ] ||
            die "existing journal belongs to another modules-dir"
        printf 'SNAPSHOT\talready-exists\t%s\n' "$JOURNAL"
        return 0
    fi

    TMP_FILE=$(mktemp "$STATE_ROOT/.journal.XXXXXX") ||
        die "cannot create journal temporary file"
    {
        printf 'version\t1\n'
        printf 'run\t%s-%s\n' "$(date +%s)" "$$"
        printf 'root\t%s\n' "$MODULES_ROOT"
        printf 'phase\tSNAPSHOT\n'
        for id in $MANAGED_IDS; do
            state=$(module_state "$id")
            if [ "$state" = missing ]; then
                exists=0
                disable=0
                remove=0
                skip_mount=0
            else
                exists=1
                disable=$(module_flag "$id" disable)
                remove=$(module_flag "$id" remove)
                skip_mount=$(module_flag "$id" skip_mount)
            fi
            printf 'record\t%s\t%s\t%s\t%s\t%s\t0\t0\n' \
                "$id" "$exists" "$disable" "$remove" "$skip_mount"
        done
    } > "$TMP_FILE" || die "cannot write snapshot journal"
    sync
    mv -f "$TMP_FILE" "$JOURNAL" || die "cannot install snapshot journal atomically"
    TMP_FILE=
    printf 'SNAPSHOT\tcreated\t%s\n' "$JOURNAL"
}

check_snapshot_state_unchanged() {
    for id in $MANAGED_IDS; do
        orig_exists=$(journal_record "$id" 3)
        orig_disable=$(journal_record "$id" 4)
        orig_remove=$(journal_record "$id" 5)
        orig_skip=$(journal_record "$id" 6)
        added=$(journal_record "$id" 7)

        state=$(module_state "$id")
        if [ "$state" = missing ]; then
            exists=0
            disable=0
            remove=0
            skip_mount=0
        else
            exists=1
            disable=$(module_flag "$id" disable)
            remove=$(module_flag "$id" remove)
            skip_mount=$(module_flag "$id" skip_mount)
        fi

        [ "$exists" = "$orig_exists" ] ||
            die "module existence changed since snapshot: $id"
        [ "$remove" = "$orig_remove" ] ||
            die "remove state changed since snapshot: $id"
        [ "$skip_mount" = "$orig_skip" ] ||
            die "skip_mount state changed since snapshot: $id"

        if [ "$added" = 1 ]; then
            [ "$disable" = 1 ] ||
                die "journal says disable was added but target is missing: $id"
        else
            [ "$disable" = "$orig_disable" ] ||
                die "disable state changed since snapshot: $id"
        fi
    done
}

disable_prepared() {
    canonical_modules_root
    canonical_state_root || die "state-dir does not exist: $STATE_DIR"
    validate_journal
    acquire_lock

    phase=$(journal_header phase)
    if [ "$phase" = DISABLED ]; then
        printf 'RESULT\talready-applied\n'
        return 0
    fi
    if [ "$phase" = RESTORED ]; then
        die "journal is already restored; take a new snapshot for a new migration"
    fi

    runtime_ready_matches_journal ||
        die "runtime-ready handoff is missing, malformed, or belongs to another run"

    validate_prepared
    validate_all_managed_targets
    check_snapshot_state_unchanged

    for id in $MANAGED_IDS; do
        orig_exists=$(journal_record "$id" 3)
        orig_disable=$(journal_record "$id" 4)
        orig_remove=$(journal_record "$id" 5)
        added=$(journal_record "$id" 7)

        [ "$added" = 1 ] && continue
        [ "$orig_exists" = 1 ] || continue
        [ "$orig_disable" = 0 ] || continue
        [ "$orig_remove" = 0 ] || continue

        prepared_ready "$id" ||
            die "enabled component is not payload-ready: $id"
    done

    for id in $MANAGED_IDS; do
        orig_exists=$(journal_record "$id" 3)
        orig_disable=$(journal_record "$id" 4)
        orig_remove=$(journal_record "$id" 5)
        added=$(journal_record "$id" 7)

        [ "$added" = 1 ] && continue
        [ "$orig_exists" = 1 ] || continue
        [ "$orig_disable" = 0 ] || continue
        [ "$orig_remove" = 0 ] || continue

        flag="$MODULES_ROOT/$id/disable"
        if ! (set -C; : > "$flag") 2>/dev/null; then
            journal_set_phase PARTIAL || true
            die "failed to create disable marker: $id"
        fi
        if ! journal_mark_record "$id" 7 1; then
            rm -f "$flag"
            die "failed to journal new disable marker: $id"
        fi
        printf 'DISABLED\t%s\n' "$id"
    done

    journal_set_phase DISABLED
    printf 'RESULT\tdisabled-prepared\n'
}

restore_flags() {
    canonical_modules_root
    canonical_state_root || die "state-dir does not exist: $STATE_DIR"
    validate_journal
    acquire_lock

    phase=$(journal_header phase)
    if [ "$phase" = RESTORED ]; then
        printf 'RESULT\talready-restored\n'
        return 0
    fi

    validate_all_managed_targets

    for id in $MANAGED_IDS; do
        added=$(journal_record "$id" 7)
        removed=$(journal_record "$id" 8)
        [ "$added" = 1 ] || continue
        [ "$removed" = 0 ] || continue
        flag="$MODULES_ROOT/$id/disable"
        path_present "$flag" || die "journal-owned disable marker is missing: $id"
        [ ! -L "$flag" ] || die "journal-owned disable marker is a symlink: $id"
        [ -f "$flag" ] || die "journal-owned disable marker is not a regular file: $id"
    done

    for id in $MANAGED_IDS; do
        added=$(journal_record "$id" 7)
        removed=$(journal_record "$id" 8)
        [ "$added" = 1 ] || continue
        [ "$removed" = 0 ] || continue
        flag="$MODULES_ROOT/$id/disable"

        rm -f "$flag" || {
            journal_set_phase RESTORE_PARTIAL || true
            die "failed to remove journal-owned disable marker: $id"
        }
        if path_present "$flag"; then
            journal_set_phase RESTORE_PARTIAL || true
            die "disable marker still exists after removal: $id"
        fi
        if ! journal_mark_record "$id" 8 1; then
            (set -C; : > "$flag") 2>/dev/null || true
            journal_set_phase RESTORE_PARTIAL || true
            die "failed to journal disable removal: $id"
        fi
        printf 'RESTORED\t%s\n' "$id"
    done

    journal_set_phase RESTORED
    printf 'RESULT\tflags-restored\n'
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --plan|--snapshot|--disable-prepared|--restore-flags)
            [ -z "$ACTION" ] || die "only one action may be specified"
            ACTION=$1
            shift
            ;;
        --modules-dir)
            [ "$#" -ge 2 ] || die "--modules-dir requires a value"
            MODULES_DIR=$2
            shift 2
            ;;
        --state-dir)
            [ "$#" -ge 2 ] || die "--state-dir requires a value"
            STATE_DIR=$2
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
done

[ -n "$ACTION" ] || {
    usage >&2
    exit 2
}
[ -n "$MODULES_DIR" ] || die "--modules-dir is required"
[ -n "$STATE_DIR" ] || die "--state-dir is required"

case "$ACTION" in
    --plan)
        print_plan
        ;;
    --snapshot)
        snapshot_plan
        ;;
    --disable-prepared)
        disable_prepared
        ;;
    --restore-flags)
        restore_flags
        ;;
esac
