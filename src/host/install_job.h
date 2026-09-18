#ifndef HOST_INSTALL_JOB_H
#define HOST_INSTALL_JOB_H

typedef enum InstallNeed {
    INSTALL_NEED_NONE = 0,
    INSTALL_NEED_PASSWORD,
    INSTALL_NEED_GUARD
} InstallNeed;

void install_job_init(const char *project_root);
void install_job_shutdown(void);
void install_job_poll(void);

const char *install_job_dir(void);
void install_job_set_dir(const char *dir);
void install_job_set_user(const char *username);
void install_job_submit_secret(const char *text);
int install_job_start(void);
void install_job_cancel(void);

int install_job_busy(void);
int install_job_need(void);
float install_job_progress(void);
const char *install_job_status(void);
int install_job_ready(void);
int install_job_launch(void);
int install_job_uninstall(void);
int install_job_verify(void);

#endif
