#ifndef APP_MINIMAL_TEST_SERIAL_H_
#define APP_MINIMAL_TEST_SERIAL_H_

int minimal_test_serial_init(void);
void minimal_test_serial_write(const char *text);
void minimal_test_serial_printf(const char *fmt, ...);

#endif /* APP_MINIMAL_TEST_SERIAL_H_ */
