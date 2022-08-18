
# Useful user-defined commands for augyang project.
# Load in gdb by command: source ../src/.gdbinit

define ay_label
    while 1
        if $arg0 == 0x0
            printf "ynode is NULL\n"
            loop_break
        end
        if ($arg0)->label == 0x0
            printf "label is NULL\n"
            loop_break
        end
        p ($arg0)->label->lens->tag
        if ($arg0)->label->lens->tag == L_LABEL || ($arg0)->label->lens->tag == L_SEQ
            printf "%s\n", ($arg0)->label->lens->string->str
            loop_break
        end
        if ($arg0)->label->lens->tag == L_KEY
            printf "%s\n", ($arg0)->label->lens->regexp->pattern->str
            loop_break
        end
        loop_break
    end
end
document ay_label
@brief Print string label in ynode.
@param[in] ynode Pointer to ynode to print label.
end

define ay_value
    while 1
        if $arg0 == 0x0
            printf "ynode is NULL\n"
            loop_break
        end
        if $arg0->value == 0x0
            printf "value is NULL\n"
            loop_break
        end
        if $arg0->value->lens->tag == L_STORE
            printf "%s\n", $arg0->value->lens->regexp->pattern->str
            loop_break
        end
        if $arg0->value->lens->tag == L_VALUE
            printf "%s\n", $arg0->value->lens->string->str
            loop_break
        end
        loop_break
    end
end
document ay_value
@brief Print string value in ynode.
@param[in] ynode Pointer to ynode to print value.
end

define ay_subtree
    set print array-indexes on
    set print array on
    p *($arg0)@($arg0)->descendants + 1
    set print array off
    set print array-indexes off
end
document ay_subtree
@brief Print ynode subtree in gdb format.
@param[in] subtree Pointer to ynode subtree.
end

define ay_siblings
    if ($arg0)->parent == 0x0
        printf "YN_ROOT has no siblings\n"
    else
        set $dbiter = $arg0->parent->child
        while $dbiter != 0x0
            if $dbiter == $arg0
                printf "$X = {INPUT_PARAMETER}\n"
            else
                p *$dbiter
            end
            set $dbiter = $dbiter->next
        end
    end
end
document ay_siblings
@brief Print all ynode siblings.
@param[in] subtree Pointer to some ynode sibling.
end

define ay_next_type
    set $dbi = 1
    set $dbe = LY_ARRAY_COUNT($arg0) - ($arg1 - $arg0)
    while $dbi < $dbe
        if ($arg1)[$dbi].type == $arg2
            p &(($arg1)[$dbi])
            loop_break
        end
        set $dbi = $dbi + 1
    end
end
document ay_next_type
@brief Find and print next ynode with @p type.
@param[in] tree Pointer to ynode tree.
@param[in] ynode Pointer to ynode from which the next ynode is searched.
@param[in] type The ynode type (enum yang_type) which is searched.
end

define ay_find
    set $dbi = 1
    while $dbi < LY_ARRAY_COUNT($arg0) 
        if $arg1 == ($arg0)[$dbi].id
            p &(($arg0)[$dbi])
            p (($arg0)[$dbi])
            loop_break
        end
        set $dbi = $dbi + 1
    end
end
document ay_find
@brief Find ynode by id.
@param[in] tree Pointer to ynode tree.
@param[in] id Identifier located in ay_ynode.id.
end

define ay_ptree
    p ay_debug_ynode_tree(1, 1, $arg0)
end
document ay_ptree
@brief Print ynode subtree by ::ay_debug_ynode_tree().
@param[in] subtree Pointer to ynode subtree to print.
end

define ay_lptree
    p ay_gdb_lptree($arg0)
    set logging overwrite on
    set logging redirect on
    set logging file $arg1.augy
    set logging enabled on
    set pagination off
    printf "%s\n", $
    set pagination on
    set logging enabled off
    set logging redirect off
    set logging overwrite off
end
document ay_lptree
@brief Print ynode subtree by ::ay_gdb_lptree() and log it to the $p file.
@param[in] subtree Pointer to ynode subtree to log.
@param[in] file Filename of logfile.
end

define ay_trans
    ay_lptree $arg0 gdb_trans/in
    next
    ay_lptree $arg0 gdb_trans/out
end
document ay_trans
@brief Log input and output of some transformation in ::ay_ynode_transformations().
In the current directory must be gdb_trans directory where the logs will be stored.
Workflow:
- Stop gdb on some transformation. Then call:
ay_trans *tree
- Then open in vim build/gdb_trans/in and call command:
:vert diffsplit build/gdb_trans/out
- For reloading 'in' and 'out' logs call:
:windo e
Or if it is the last command then just @:
@param[in] tree Pointer to ynode tree to log.
end

define ay_get_module
    set $mod = $arg0->modules
    if $_streq($mod->name, $arg1)
        p *$mod
    end

    while $mod->next
        if $_streq($mod->name, $arg1)
            p *$mod
        end
        set $mod = $mod->next
    end
end
document ay_get_module
@brief Take the augeas structure and search for the specified module.
@param[in] aug Pointer to struct augeas.
@param[in] name String containing the name of the module to be found.
end
