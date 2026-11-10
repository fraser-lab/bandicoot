# bandicoot_state.py
#
# Saved states: the models of a state are kept, as they were when it was
# saved, in a gzipped tarball beside the state file. The state file writes the
# calls below; this file defines only functions.

import os
import shutil
import tarfile
import tempfile


class _StateRestore(object):
    """What a state being read needs: where its models were unpacked, and the
    number its molecule 0 becomes (past any molecules already loaded)."""

    def __init__(self, base):
        self.base = base
        self.directory = None


def bandicoot_state_bundle(staging_dir, tarball):
    """Pack every file in staging_dir into tarball (gzipped). Written under a
    temporary name and then moved into place, so an earlier tarball survives a
    write that fails. True on success."""
    tmp = None
    try:
        folder = os.path.dirname(os.path.abspath(tarball))
        fd, tmp = tempfile.mkstemp(prefix=".bandicoot-state-", suffix=".tar.gz", dir=folder)
        os.close(fd)
        with tarfile.open(tmp, "w:gz") as t:
            for name in sorted(os.listdir(staging_dir)):
                t.add(os.path.join(staging_dir, name), arcname=name)
        # mkstemp makes the file private; give it the permissions any new file gets
        mask = os.umask(0)
        os.umask(mask)
        os.chmod(tmp, 0o666 & ~mask)
        os.replace(tmp, tarball)
        return True
    except Exception as e:
        print("ERROR:: state: could not write %s: %s" % (tarball, e))
        if tmp and os.path.exists(tmp):
            try:
                os.remove(tmp)
            except Exception:
                pass
        return False


def bandicoot_state_restore_begin(tarball):
    """Start reading a state: unpack its tarball (None if it has no models).
    The file choosers then open in the state's folder."""
    restore = _StateRestore(graphics_n_molecules())
    if tarball is None:
        return restore
    path = tarball
    if not os.path.isfile(path):
        # the state's folder has moved: look beside the working directory
        alternative = os.path.basename(tarball)
        if os.path.isfile(alternative):
            path = alternative
        else:
            print("ERROR:: state: no file %s; its models cannot be read" % tarball)
            return restore
    folder = os.path.dirname(os.path.abspath(path))
    bandicoot_set_file_chooser_directory(folder)
    try:
        restore.directory = tempfile.mkdtemp(prefix="bandicoot-state-")
        with tarfile.open(path, "r:gz") as t:
            t.extractall(restore.directory, filter="data")
    except Exception as e:
        print("ERROR:: state: could not read %s: %s" % (path, e))
        bandicoot_state_restore_end(restore)
    return restore


def bandicoot_state_slot(restore, saved_imol):
    """Make the next molecule read take number base + saved_imol, leaving
    empty slots where the saved state had closed molecules."""
    bandicoot_fill_molecule_slots(restore.base + saved_imol)


def bandicoot_state_read_model(restore, saved_imol, member, name):
    """Read a model from the state's tarball as molecule base + saved_imol,
    under the name it had when the state was saved."""
    bandicoot_state_slot(restore, saved_imol)
    if restore.directory is None:
        return -1
    path = os.path.join(restore.directory, member)
    if not os.path.isfile(path):
        print("ERROR:: state: %s is not in the state's tarball" % member)
        return -1
    imol = handle_read_draw_molecule_with_recentre(path, 0)
    if is_valid_model_molecule(imol):
        set_molecule_name(imol, name)
        if imol != restore.base + saved_imol:
            print("WARNING:: state: %s read as molecule %d, not %d"
                  % (name, imol, restore.base + saved_imol))
    else:
        print("ERROR:: state: could not read %s" % member)
    return imol


def bandicoot_state_read_dictionary(restore, file_name, saved_imol, applied_as):
    """Read a dictionary back for the molecule it was read for (offset as the
    models are; a negative number means every molecule), applied without
    asking under the comp ids it was applied under when the state was saved."""
    imol = saved_imol if saved_imol < 0 else restore.base + saved_imol
    return bandicoot_read_cif_dictionary_for_state(file_name, imol, applied_as)


def bandicoot_state_restore_end(restore):
    """The models are read: remove the files they were unpacked into."""
    if restore.directory:
        shutil.rmtree(restore.directory, ignore_errors=True)
        restore.directory = None
