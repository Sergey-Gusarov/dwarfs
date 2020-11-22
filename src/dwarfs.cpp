/* vim:set ts=2 sw=2 sts=2 et: */
/**
 * \author     Marcus Holland-Moritz (github@mhxnet.de)
 * \copyright  Copyright (c) Marcus Holland-Moritz
 *
 * This file is part of dwarfs.
 *
 * dwarfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dwarfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with dwarfs.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <stdexcept>

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include <boost/filesystem.hpp>

#include <folly/Conv.h>

#include <fuse3/fuse_lowlevel.h>

#include "dwarfs/filesystem.h"
#include "dwarfs/inode_reader.h"
#include "dwarfs/metadata.h"
#include "dwarfs/mmap.h"
#include "dwarfs/options.h"
#include "dwarfs/util.h"

namespace dwarfs {

struct options {
  const char* progname;
  std::string fsimage;
  int seen_mountpoint;
  const char* cachesize_str;        // TODO: const?? -> use string?
  const char* debuglevel_str;       // TODO: const?? -> use string?
  const char* workers_str;          // TODO: const?? -> use string?
  const char* decompress_ratio_str; // TODO: const?? -> use string?
  size_t cachesize;
  size_t workers;
  double decompress_ratio;
  logger::level_type debuglevel;
  struct ::stat stat_defaults;
};

// #define DEBUG_FUNC(x) std::cerr << __func__ << "(" << x << ")" << std::endl;
#define DEBUG_FUNC(x)

// TODO: better error handling

#define DWARFS_OPT(t, p, v)                                                    \
  { t, offsetof(struct options, p), v }

const struct fuse_opt dwarfs_opts[] = {
    // TODO: user, group, atime, mtime, ctime for those fs who don't have it?
    //       second level cachesize
    DWARFS_OPT("cachesize=%s", cachesize_str, 0),
    DWARFS_OPT("debuglevel=%s", debuglevel_str, 0),
    DWARFS_OPT("workers=%s", workers_str, 0),
    DWARFS_OPT("decratio=%s", decompress_ratio_str, 0), FUSE_OPT_END};

options opts;
stream_logger s_lgr(std::cerr);
std::shared_ptr<filesystem> s_fs;

void op_init(void* /*userdata*/, struct fuse_conn_info* /*conn*/) {
  DEBUG_FUNC("")
  block_cache_options bco;
  bco.max_bytes = opts.cachesize;
  bco.num_workers = opts.workers;
  bco.decompress_ratio = opts.decompress_ratio;
  s_fs =
      std::make_shared<filesystem>(s_lgr, std::make_shared<mmap>(opts.fsimage),
                                   bco, &opts.stat_defaults, FUSE_ROOT_ID);
}

void op_destroy(void* /*userdata*/) {
  DEBUG_FUNC("")
  s_fs.reset();
}

void op_lookup(fuse_req_t req, fuse_ino_t parent, const char* name) {
  DEBUG_FUNC(parent << ", " << name)

  int err = ENOENT;

  try {
    auto de = s_fs->find(parent, name);

    if (de) {
      struct ::fuse_entry_param e;

      err = s_fs->getattr(de, &e.attr);

      if (err == 0) {
        e.generation = 1;
        e.ino = e.attr.st_ino;
        e.attr_timeout = std::numeric_limits<double>::max();
        e.entry_timeout = std::numeric_limits<double>::max();

        fuse_reply_entry(req, &e);

        return;
      }
    }
  } catch (const dwarfs::error& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info*) {
  DEBUG_FUNC(ino)

  int err = ENOENT;

  // TODO: merge with op_lookup
  try {
    auto de = s_fs->find(ino);

    if (de) {
      struct ::stat stbuf;

      err = s_fs->getattr(de, &stbuf);

      if (err == 0) {
        fuse_reply_attr(req, &stbuf, std::numeric_limits<double>::max());

        return;
      }
    }
  } catch (const dwarfs::error& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_access(fuse_req_t req, fuse_ino_t ino, int mode) {
  DEBUG_FUNC(ino)

  int err = ENOENT;

  // TODO: merge with op_lookup
  try {
    auto de = s_fs->find(ino);

    if (de) {
      auto ctx = fuse_req_ctx(req);
      err = s_fs->access(de, mode, ctx->uid, ctx->gid);
    }
  } catch (const dwarfs::error& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_readlink(fuse_req_t req, fuse_ino_t ino) {
  DEBUG_FUNC(ino)

  int err = ENOENT;

  try {
    auto de = s_fs->find(ino);

    if (de) {
      std::string str;

      err = s_fs->readlink(de, &str);

      if (err == 0) {
        fuse_reply_readlink(req, str.c_str());

        return;
      }
    }
  } catch (const dwarfs::error& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi) {
  DEBUG_FUNC(ino)

  int err = ENOENT;

  try {
    auto de = s_fs->find(ino);

    if (de) {
      if (S_ISDIR(de->mode)) {
        err = EISDIR;
      } else if (fi->flags & (O_APPEND | O_CREAT | O_TRUNC)) {
        err = EACCES;
      } else {
        fi->fh = reinterpret_cast<intptr_t>(de);
        fi->keep_cache = 1;
        fuse_reply_open(req, fi);
        return;
      }
    }
  } catch (const dwarfs::error& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
             struct fuse_file_info* fi) {
  DEBUG_FUNC(ino << ", " << size << ", " << off)

  int err = ENOENT;

  try {
    auto de = reinterpret_cast<const dir_entry*>(fi->fh);

    if (de) {
      iovec_read_buf buf;
      ssize_t rv = s_fs->readv(ino, buf, size, off);

      // std::cerr << ">>> " << rv << std::endl;

      if (rv >= 0) {
        fuse_reply_iov(req, buf.buf.empty() ? nullptr : &buf.buf[0],
                       buf.buf.size());

        return;
      }

      err = -rv;
    }
  } catch (const dwarfs::error& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                struct fuse_file_info* /*fi*/) {
  DEBUG_FUNC(ino << ", " << size << ", " << off)

  int err = ENOENT;

  try {
    auto de = s_fs->find(ino);

    if (de) {
      auto d = s_fs->opendir(de);

      if (d) {
        off_t lastoff = s_fs->dirsize(d);
        std::string name;
        struct stat stbuf;
        std::vector<char> buf(size);
        size_t written = 0;

        while (off < lastoff) {
          auto de = s_fs->readdir(d, off, &name);
          s_fs->getattr(de, &stbuf);

          /// std::cerr << ">>> " << off << "/" << lastoff << " - " << name << "
          /// - " << stbuf.st_ino << std::endl;

          size_t needed =
              fuse_add_direntry(req, &buf[written], buf.size() - written,
                                name.c_str(), &stbuf, off + 1);

          if (written + needed > size) {
            break;
          }

          written += needed;
          ++off;
        }

        fuse_reply_buf(req, written > 0 ? &buf[0] : nullptr, written);

        return;
      }

      err = ENOTDIR;
    }
  } catch (const dwarfs::error& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void op_statfs(fuse_req_t req, fuse_ino_t /*ino*/) {
  DEBUG_FUNC("")

  int err = EIO;

  try {
    struct ::statvfs buf;

    err = s_fs->statvfs(&buf);

    if (err == 0) {
      fuse_reply_statfs(req, &buf);

      return;
    }
  } catch (const dwarfs::error& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = e.get_errno();
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    err = EIO;
  }

  fuse_reply_err(req, err);
}

void usage(const char* progname) {
  std::cerr << "dwarfs (c) Marcus Holland-Moritz\n\n"
            << "usage: " << progname << " image mountpoint [options]\n\n"
            << "DWARFS options:\n"
            << "    -o cachesize=SIZE      set size of block cache (512M)\n"
            << "    -o workers=NUM         number of worker threads (2)\n"
            << "    -o decratio=NUM        ratio for full decompression (0.8)\n"
            << "    -o debuglevel=NAME     error, warn, info, debug, trace\n"
            << std::endl;

  fuse_cmdline_help();

  ::exit(1);
}

int option_hdl(void* data, const char* arg, int key,
               struct fuse_args* /*outargs*/) {
  options* opts = reinterpret_cast<options*>(data);

  switch (key) {
  case FUSE_OPT_KEY_NONOPT:
    if (opts->seen_mountpoint) {
      return -1;
    }

    if (!opts->fsimage.empty()) {
      opts->seen_mountpoint = 1;
      return 1;
    }

    opts->fsimage = boost::filesystem::canonical(arg).native();

    return 0;

  case FUSE_OPT_KEY_OPT:
    if (::strncmp(arg, "-h", 2) == 0 || ::strncmp(arg, "--help", 6) == 0) {
      usage(opts->progname);
    }
    break;
  }

  return 1;
}

int run_fuse(struct fuse_args& args) {
  struct fuse_cmdline_opts fuse_opts;

  if (fuse_parse_cmdline(&args, &fuse_opts) == -1 || !fuse_opts.mountpoint) {
    usage(opts.progname);
  }

  struct fuse_lowlevel_ops fsops;

  ::memset(&fsops, 0, sizeof(fsops));

  fsops.init = op_init;
  fsops.destroy = op_destroy;
  fsops.lookup = op_lookup;
  fsops.getattr = op_getattr;
  fsops.access = op_access;
  fsops.readlink = op_readlink;
  fsops.open = op_open;
  fsops.read = op_read;
  fsops.readdir = op_readdir;
  fsops.statfs = op_statfs;
  // fsops.getxattr = op_getxattr;
  // fsops.listxattr = op_listxattr;

  auto se = fuse_session_new(&args, &fsops, sizeof(fsops), nullptr);
  int err = 1;

  if (se) {
    if (fuse_set_signal_handlers(se) == 0) {
      if (fuse_session_mount(se, fuse_opts.mountpoint) == 0) {
        if (fuse_daemonize(fuse_opts.foreground) == 0) {
          if (fuse_opts.singlethread) {
            err = fuse_session_loop(se);
          } else {
            struct fuse_loop_config config;
            config.clone_fd = fuse_opts.clone_fd;
            config.max_idle_threads = fuse_opts.max_idle_threads;
            err = fuse_session_loop_mt(se, &config);
          }
        }
        fuse_session_unmount(se);
      }
      fuse_remove_signal_handlers(se);
    }
    fuse_session_destroy(se);
  }

  ::free(fuse_opts.mountpoint);
  fuse_opt_free_args(&args);

  return err;
}
} // namespace dwarfs

int main(int argc, char* argv[]) {
  using namespace dwarfs;

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

  opts.progname = argv[0];

  fuse_opt_parse(&args, &opts, dwarfs_opts, option_hdl);

  opts.cachesize = opts.cachesize_str ? parse_size_with_unit(opts.cachesize_str)
                                      : (static_cast<size_t>(512) << 20);
  // TODO: foreground mode, stderr vs. syslog?
  opts.debuglevel = opts.debuglevel_str
                        ? logger::parse_level(opts.debuglevel_str)
                        : logger::INFO;
  opts.workers = opts.workers_str ? folly::to<size_t>(opts.workers_str) : 2;
  opts.decompress_ratio = opts.decompress_ratio_str
                              ? folly::to<double>(opts.decompress_ratio_str)
                              : 0.8;

  s_lgr.set_threshold(opts.debuglevel);
  log_proxy<debug_logger_policy> log(s_lgr);

  log.info() << "dwarfs (" << DWARFS_VERSION << ")";

  if (!opts.seen_mountpoint) {
    usage(opts.progname);
  }

  metadata::get_stat_defaults(&opts.stat_defaults);

  return run_fuse(args);
}
