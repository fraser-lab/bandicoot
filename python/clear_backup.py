#    clear_backup.py -- tidy up old files in coot-backup/
#
#    Copyright (C) <year>  <name of author>
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.

# BANDICOOT v0.2 (2026-08-12): converted from Python 2 and repaired.
#
# This script had not run at all under Bandicoot's Python 3.13 interpreter:
# clear_backups_maybe() called operator.isNumberType(), removed in Python 3, so
# it raised AttributeError immediately. run_clear_backups_py() then printed its
# "returns None" warning and exited, and no backup was ever cleaned up. Four
# further defects were found while converting, listed at each fix below.

import os
import glob
import time

global clear_out_backup_run_n_days
global clear_out_backup_old_days

clear_out_backup_run_n_days = 7     # run every 7 days
clear_out_backup_old_days   = 7     # Files older than 7 days are considered
                                    # for deletion

# The coordinate formats a backup can be written in.
#
# FIX (Bandicoot v0.2): this used to be "*.pdb*" alone, so mmCIF and SHELX
# backups were NEVER cleaned up -- they grew without bound. make_backup() picks
# the backup's format from the molecule's original file
# (molecule_class_info_t::get_save_molecule_filename), so a molecule read from
# mmCIF is backed up as ".cif", from SHELX as ".res", and ".gz" is appended
# whenever backup_compress_files_flag is set (it is, by default). An mmCIF
# backup is also about 1.85x the size of the PDB equivalent, so ignoring them
# was the worst of both.
#
# Deliberately explicit rather than a catch-all glob: the backup directory also
# holds the "last-cleaned" stamp file, which must not be swept up.
backup_patterns = ["*.pdb", "*.pdb.gz",
                   "*.cif", "*.cif.gz",
                   "*.res", "*.res.gz"]


def backup_files_in_dir(directory):
    """Every backup coordinate file in directory, whatever format it is in."""
    found = []
    for pattern in backup_patterns:
        found += glob.glob(os.path.normpath(os.path.join(directory, pattern)))
    return found


def backup_dirs():
    """The backup directories in play: $COOT_BACKUP_DIR and/or ./coot-backup."""
    dirs = []
    backup_env = os.getenv('COOT_BACKUP_DIR')
    if backup_env:
        dirs.append(backup_env)
    if os.path.isdir("coot-backup"):
        dirs.append(os.path.normpath("./coot-backup"))
    return dirs


def delete_coot_backup_files(action_type):

    global clear_out_backup_old_days

    dirs = backup_dirs()
    files = []
    for directory in dirs:
        files += backup_files_in_dir(directory)
    dir_str = " and ".join(dirs)

    now = int(time.time())
    # FIX: operator.isNumberType() was removed in Python 3 -- this raised
    # AttributeError and killed the whole cleanup.
    if not isinstance(clear_out_backup_old_days, (int, float)):
        clear_out_backup_old_days = 7
    n_days = clear_out_backup_old_days
    cutoff = now - (n_days * 24 * 60 * 60)

    def old_files_list(files):
        old_files = []
        for f in files:
            try:
                if os.path.getmtime(f) < cutoff:
                    old_files.append(f)
            except OSError:
                pass          # vanished under us; nothing to clean
        return old_files

    olds = old_files_list(files)

    if action_type == "count":
        total_size = 0
        for f in olds:
            try:
                total_size += os.path.getsize(f)
            except OSError:
                pass
        mb_size = total_size / (1024. * 1024)
        return [len(olds), mb_size]

    if action_type == 'delete':
        # FIX: this iterated over `files`, not `olds` -- so "delete old
        # backups" deleted EVERY backup, including the one written seconds ago,
        # while the dialog reported only the old ones. The count and the
        # deletion disagreed.
        for f in olds:
            dir_name, file_name = os.path.split(f)
            print("INFO:: deleting old backup %s from %s" % (file_name, dir_name))
            try:
                os.remove(f)
            except OSError as e:
                print("WARNING:: could not delete %s (%s)" % (f, e))
        # now create a last-backup file with a time stamp:
        for directory in dirs:
            if os.path.isdir(directory):
                last_cleaned_file = os.path.normpath(os.path.join(directory, "last-cleaned"))
                try:
                    with open(last_cleaned_file, 'w') as fout:
                        fout.write(str(int(time.time())))
                except IOError as e:
                    print("WARNING:: could not write %s (%s)" % (last_cleaned_file, e))


# Make a GUI
#
# return True or False depending on if the GUI dialog was shown (it isn't
# shown if there are no files to delete).
#
def clear_backup_gui():

    import gtk

    file_stats = delete_coot_backup_files("count")

    # more than 1 file to possibly delete?
    if file_stats[0] == 0:
        return False   # didn't run

    window = gtk.Window(gtk.WINDOW_TOPLEVEL)
    frame = gtk.Frame("Old Backups")
    vbox = gtk.VBox(False, 3)
    hbox = gtk.HBox(False, 10)
    ok_button = gtk.Button(" Clear up ")
    cancel_button = gtk.Button(" Stay messy ")
    h_sep = gtk.HSeparator()
    label_str = "  There are " + str(file_stats[0]) + \
                " old backup files (%.1fMb) \n" % file_stats[1] + "   Delete Them?"
    label = gtk.Label(label_str)

    # FIX: the handler was
    #     lambda w: map(eval, ["delete_coot_backup_files('delete')", ...])
    # and in Python 3 map() is LAZY -- nothing was ever consumed, so clicking
    # "Clear up" evaluated nothing at all and simply did not exit. Call them.
    def on_ok(widget):
        delete_coot_backup_files('delete')
        coot_real_exit(0)

    ok_button.connect("clicked", on_ok)
    cancel_button.connect("clicked", lambda w: coot_real_exit(0))

    ok_text = " Consider yourself patted on the back! "
    cancel_text = "A less pejorative label here might be \"Keep\" or \"Cancel\" " + \
                  "but seeing as (for the moment) I like my intestines where they are " + \
                  "and not used as hosiery fastenings for Systems Adminstrators then " + \
                  "we get this rather nannying label..."
    ok_button.set_tooltip_text(ok_text)
    cancel_button.set_tooltip_text(cancel_text)

    window.add(frame)
    frame.set_border_width(6)
    frame.add(vbox)
    vbox.pack_start(label, False, False, 6)
    vbox.pack_start(h_sep)
    vbox.pack_start(hbox)
    hbox.set_border_width(10)
    vbox.set_border_width(6)
    hbox.pack_start(ok_button, True, True, 6)
    hbox.pack_start(cancel_button, True, True, 6)

    # FIX: set_flags(gtk.CAN_DEFAULT) was removed in PyGTK 2.22.
    ok_button.set_can_default(True)
    cancel_button.set_can_default(True)
    ok_button.grab_default()

    window.show_all()
    return True  # it ran


# return a status, True or False, did the gui run?
#
# If this function returns False, then coot_real_exit() just exits with
# coot_real_exit().  Otherwise we wait for the GUI.
#
def clear_backups_maybe():

    global clear_out_backup_run_n_days

    now = int(time.time())
    # FIX: operator.isNumberType() again -- this is the one that actually fired
    # on every exit, because this is the entry point run_clear_backups_py()
    # calls.
    if not isinstance(clear_out_backup_run_n_days, (int, float)):
        clear_out_backup_run_n_days = 7
    n_days = clear_out_backup_run_n_days
    cutoff = now - (n_days * 24 * 60 * 60)

    dirs = backup_dirs()

    all_last_cleaned_files = []
    for directory in dirs:
        last_cleaned_file = os.path.join(directory, "last-cleaned")
        if os.path.isfile(last_cleaned_file):
            all_last_cleaned_files.append(last_cleaned_file)

    # never cleaned here before: offer to
    if len(all_last_cleaned_files) == 0:
        return clear_backup_gui()

    for last_cleaned_file in all_last_cleaned_files:
        try:
            with open(last_cleaned_file, 'r') as fin:
                ival = int(fin.read().strip())
        except (IOError, ValueError):
            continue          # unreadable stamp: try the next directory
        if ival < cutoff:
            return clear_backup_gui()
        # FIX: this was  (now - val) * 24 * 60 * 60  with val a STRING --
        # a TypeError, and the arithmetic was inverted anyway (multiplying
        # rather than dividing gives a nonsense "days ago").
        days_ago = (now - ival) // (24 * 60 * 60)
        print("INFO:: backup clearout done %s days ago" % days_ago)

    return False
