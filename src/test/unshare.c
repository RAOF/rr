/* -*- Mode: C; tab-width: 8; c-basic-offset: 2; indent-tabs-mode: nil; -*- */

#include "rrutil.h"

extern int capset(cap_user_header_t header, const cap_user_data_t data);

static char tmp_name[] = "/tmp/rr-unshare-tmp-XXXXXX";

static void* start_thread(__attribute__((unused)) void* p) {
  test_assert(0 == unshare(CLONE_FILES));
  test_assert(0 == close(STDOUT_FILENO));
  return NULL;
}

static void test_mount(void) {
  test_assert(0 == mkdir("/aaa", 0777));
  /* Most filesystems can't be mounted in a non-root namespace,
     but proc can. */
  test_assert(0 == mount("dummy", "/aaa", "proc", 0, NULL));
  test_assert(0 == access("/aaa/cpuinfo", F_OK));
  test_assert(0 == umount("/aaa"));
  test_assert(0 == mount("dummy", "/aaa", "proc", 0, NULL));
  test_assert(0 == umount2("/aaa", 0));
  test_assert(0 == rmdir("/aaa"));
}

static void test_privileged(void) {
  struct __user_cap_header_struct hdr = { _LINUX_CAPABILITY_VERSION_1, 0 };
  struct __user_cap_data_struct data = { 0x1, 0x1, 0x1 };

  /* Run privileged tests *before* we reset our privileges below */
  test_mount();

  /* Test using capset. capset is privileged, but we are privileged
     in our user namespace. */
  test_assert(0 == capset(&hdr, &data));
}

static void run_child(void) {
  pid_t child = fork();
  int status;

  if (!child) {
    /* Test creating a nested child */
    pid_t nested_child = fork();
    if (!nested_child) {
      exit(77);
    }
    test_assert(nested_child == wait(&status));
    test_assert(WIFEXITED(status) && 77 == WEXITSTATUS(status));

    /* Test creating a thread */
    pthread_t thread;
    pthread_create(&thread, NULL, start_thread, NULL);
    pthread_join(thread, NULL);

    test_privileged();

    /* stdout should still be writable due to the unshare() */
    test_assert(13 == write(STDOUT_FILENO, "EXIT-SUCCESS\n", 13));
    exit(55);
  }
  test_assert(child == wait(&status));
  test_assert(WIFEXITED(status) && 55 == WEXITSTATUS(status));
}

static int run_test(void) {
  int ret;
  int fd;
  struct rlimit nofile;

  /* Emulate what sandboxes trying to close all open file descriptors */
  test_assert(0 == getrlimit(RLIMIT_NOFILE, &nofile));
  for (fd = STDOUT_FILENO + 1; fd < (int)nofile.rlim_cur; ++fd) {
    ret = close(fd);
    test_assert(ret == 0 || (ret == -1 && errno == EBADF));
  }

  ret = unshare(CLONE_NEWUSER);
  if (ret == -1 && (errno == EINVAL || errno == EPERM)) {
    atomic_puts("EXIT-SUCCESS");
    return 77;
  }
  test_assert(0 == ret);

  test_assert(0 == unshare(CLONE_NEWNS));
  test_assert(0 == unshare(CLONE_NEWIPC));
  test_assert(0 == unshare(CLONE_NEWNET));
  ret = unshare(CLONE_NEWPID);
  if (ret == -1 && errno == EINVAL) {
    atomic_puts("EXIT-SUCCESS");
    return 77;
  }

  test_assert(0 == ret);

  test_assert(0 == chroot(tmp_name));

  run_child();
  return 77;
}

int main(void) {
  pid_t child;
  int ret;
  int status;

  test_assert(tmp_name == mkdtemp(tmp_name));

  child = fork();
  if (!child) {
    return run_test();
  }
  ret = wait(&status);
  test_assert(0 == rmdir(tmp_name));
  test_assert(child == ret);
  test_assert(WIFEXITED(status) && 77 == WEXITSTATUS(status));

  return 0;
}