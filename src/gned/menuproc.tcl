#==========================================================================
#  MENUPROC.TCL -
#            part of the GNED, the Tcl/Tk graphical topology editor of
#                            OMNeT++
#   By Andras Varga
#==========================================================================

#----------------------------------------------------------------#
#  Copyright (C) 1992,99 Andras Varga
#  Technical University of Budapest, Dept. of Telecommunications,
#  Stoczek u.2, H-1111 Budapest, Hungary.
#
#  This file is distributed WITHOUT ANY WARRANTY. See the file
#  `license' for details on this and other legal matters.
#----------------------------------------------------------------#


proc fileNewNedfile {} {
   global gned

   # create a new module in a new file
   set nedfilekey [addItem nedfile 0]
   set modkey [addItem module $nedfilekey]
   openModuleOnNewCanvas $modkey

   updateTreeManager
}

proc fileNewComponent {type} {
   global gned ned canvas

   # get nedfile of module on current canvas,
   # and create a new module in that file
   set canv_id $gned(canvas_id)
   set curmodkey $canvas($canv_id,module-key)
   set nedfilekey $ned($curmodkey,parentkey)

   set key [addItem $type $nedfilekey]
   markNedfileOfItemDirty $key

   if {$type=="module"} {
       openModuleOnNewCanvas $key
   }

   updateTreeManager
}

proc fileOpen {{fname ""}} {
   global gned env canvas ned

   catch {cd [file dirname $fname]}
   set fname [file tail $fname]
   set fname [tk_getOpenFile -defaultextension ".ned" \
              -initialdir [pwd] -initialfile $fname \
              -filetypes {{{NED files} {*.ned}} {{All files} {*}}}]

   if {$fname!=""} {
      # regsub "^$env(HOME)/" $fname "~/" fname

      set type [file extension $fname]
      if {$type==".ned"} {
         loadNED $fname
      } else {
         tk_messageBox -icon warning -type ok \
                       -message "Don't know how to open $type files."
      }
   }
}

proc fileSave {{nedfilekey {}}} {
   global ned gned canvas

   if {$nedfilekey==""} {
      # default: current canvas
      set canv_id $gned(canvas_id)
      set modkey $canvas($canv_id,module-key)
      set nedfilekey $ned($modkey,parentkey)

      if {$ned($nedfilekey,type)!="nedfile"} {error "internal error in fileSave"}
   }

   if {!$ned($nedfilekey,unnamed)} {
      saveNED $nedfilekey
      nedfileClearDirty $nedfilekey
   } else {
      fileSaveAs
   }
}

proc fileSaveAs {{nedfilekey {}}} {
   global gned canvas ned env

   if {$nedfilekey==""} {
      # default: current canvas
      set canv_id $gned(canvas_id)
      set modkey $canvas($canv_id,module-key)
      set nedfilekey $ned($modkey,parentkey)
   }

   if {$ned($nedfilekey,filename)!=""} {
      set fname $ned($nedfilekey,filename)
   } elseif [info exist modkey] {
      set fname "$ned($modkey,name).ned"
   } else {
      set fname "unnamed.ned"
   }

   catch {cd [file dirname $fname]}
   set fname [file tail $fname]
   set fname [tk_getSaveFile -defaultextension ".ned" \
              -initialdir [pwd] -initialfile $fname \
              -filetypes {{{NED files} {*.ned}} {{All files} {*}}}]

   if {$fname!=""} {
      # regsub "^$env(HOME)/" $fname "~/" fname

      set ned($nedfilekey,unnamed) 0
      set ned($nedfilekey,name) [makeFancyName $fname]
      set ned($nedfilekey,filename) $fname

      adjustWindowTitle
      updateTreeManager

      saveNED $nedfilekey
      nedfileClearDirty $nedfilekey
   }
}

proc fileCloseNedfile {{nedfilekey {}}} {
   global canvas gned ned

   if {$nedfilekey==""} {
      # default: current canvas
      set canv_id $gned(canvas_id)
      set modkey $canvas($canv_id,module-key)
      set nedfilekey $ned($modkey,parentkey)
   }

   # offer saving it
   if [nedfileIsDirty $nedfilekey] {
       if {$ned($nedfilekey,unnamed)} {
          set reply [tk_messageBox -title "Last chance" -icon warning -type yesno \
                -message "Unnamed file not saved yet. Save it now?"]
          if {$reply=="yes"} fileSave
       } else {
          set fname $ned($nedfilekey,filename)
          set fname [file tail $fname]
          set reply [tk_messageBox -title "Last chance" -icon warning -type yesno \
                -message "File $fname contains unsaved changes. Save?"]
          if {$reply=="yes"} fileSave
       }
   }

   # delete from memory
   deleteNedfile $nedfilekey
   updateTreeManager
}

proc fileCloseCanvas {} {
   closeCurrentCanvas
   updateTreeManager
}

proc fileExit {} {
   global ned

   # close all ned files
   foreach key $ned(0,childrenkeys) {
       if $ned($key,dirty) {
           fileCloseNedfile $key
       }
   }
   opp_exit
}

proc editCut {} {

   tk_messageBox -title "GNED" -icon info -type ok -message "NED clipboard doesn't work yet. Sorry."
   return

   editCopy
   deleteSelected
}

proc editCopy {} {

   tk_messageBox -title "GNED" -icon info -type ok -message "NED clipboard doesn't work yet. Sorry."
   return

   global clipboard_ned ned

   set selection [selectedItems]

   # accept only submodules and connections whose both ends will be copied
   set keys {}
   foreach key $selection {
      if {$ned($key,type)=="submod"} {
         lappend keys $key
      } elseif {$ned($key,type)=="conn"} {
         if {[lsearch $selection $ned($key,src-ownerkey)]!=-1 &&
             [lsearch $selection $ned($key,dest-ownerkey)]!=-1} \
         {
             lappend keys $key
         }
      }
   }

   # copy out items to clipboard
   copyArrayFromNed clipboard_ned $keys
   return $keys
}

proc editPaste {} {

   tk_messageBox -title "GNED" -icon info -type ok -message "NED clipboard doesn't work yet. Sorry."
   return

   global clipboard_ned ned gned canvas

   deselectAllItems

   # offset x-pos and y-pos
   foreach i [array names clipboard_ned "*,?-pos"] {
      set clipboard_ned($i) [expr $clipboard_ned($i)+8]
   }

   set new_keys [pasteArrayIntoNed clipboard_ned]

   # make items owned by the module on this canvas
   set modkey $canvas($gned(canvas_id),module-key)
   foreach key $new_keys {
      set ned($key,module-ownerkey) $modkey
      selectItem $key
   }

   # draw pasted items on canvas
   drawItems $new_keys
}

proc editDelete {} {
   deleteSelected
}

proc editCheck {} {
   tk_messageBox -title "GNED" -icon warning -type ok \
                 -message "Consistency Check not implemented yet.\
                           It should check that submodule parameters and gates\
                           are consistent with earlier module declarations and offer\
                           making automatic corrections."
}

#proc optionsLoadBackground {} {
#   global gned
#
#   tk_messageBox -title "Background files" -icon info -type ok \
#      -message "Background files are Tcl scripts that draw on canvas \$c. The file you specify will simply be sourced. You can load maps, floorplans etc."
#
#   set fname [tk_getOpenFile -defaultextension "tcl" -title "Load background" \
#              -filetypes {{{Tcl files} {*.tcl}}  {{All files} {*}}}]
#
#   if {$fname!=""} {
#       set c $gned(canvas)
#       if [catch {source $fname} errmsg] {
#          tk_messageBox -title GNED -icon warning -type ok -message "Error: $errmsg"
#       }
#   }
#}

proc toggleGrid {setvar} {
    global gned

    if {$setvar} {
        if {$gned(snaptogrid)} {
            set gned(snaptogrid) 0
        } else {
            set gned(snaptogrid) 1
        }
    }

    if {$gned(snaptogrid)} {
        $gned(toolbar).grid config -relief sunken
    } else {
        $gned(toolbar).grid config -relief raised
    }
}

proc optionsViewFile {} {
    global env

    set fname [tk_getOpenFile -defaultextension "" \
              -filetypes {{{All files} {*}}}]

    if {$fname!=""} {
       # regsub "^$env(HOME)/" $fname "~/" fname
       createFileViewer $fname
    }
}

proc helpAbout {} {
    createOkCancelDialog .about "About OMNeT++/GNED"

    label .about.f.l -text \
{
GNED 1.2 Beta 1
Part of the OMNeT++ Discrete Event Simulator

(C) 1997-99 Andras Varga

NO WARRANTY. See Help|Release notes and the 'license' file for details.
}

    pack .about.f.l -anchor center -expand 0 -fill x -side top

    execOkCancelDialog .about
    destroy .about
}

proc helpRelNotes {} {
    global OMNETPP_GNED_DIR
    createFileViewer [file join $OMNETPP_GNED_DIR "readme"]
}


