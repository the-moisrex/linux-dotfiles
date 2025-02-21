#!/usr/bin/env python3

import os
import subprocess
import sys

import psutil
from PyQt6.QtCore import QProcess, QSettings, QTimer, Qt
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QMainWindow,
    QMessageBox,
    QMenu,
    QPushButton,
    QStatusBar,
    QTextEdit,
    QVBoxLayout,
    QWidget,
    QListWidgetItem,
    QStyle,
)


class SigGUI(QMainWindow):
    """GUI application for sending signals to processes."""

    USELESS_PROCESS_NAMES = [
        "systemd",
        "plasmashell",
        "(sd-pam)",
        "sudo",
        "kwin_x11",
        "kded5",
        "ksmserver",
        "dbus-daemon",
        "xdg-desktop-portal",
        "xdg-permission-store",
        "xdg-document-portal",
        "xdg-user-dirs",
        "pipewire",
        "wireplumber",
        "pulseaudio",
        "NetworkManager",
        "wpa_supplicant",
        "avahi-daemon",
        "cupsd",
        "gsd-",
        "gnome-session-",
        "x-session-manager",
        "init",
        "dockerd",
        "containerd",
        "snapd",
        "udevd",
        "journald",
        "rsyslogd",
        "crond",
        "atd",
        "sshd",
        "agetty",
        "login",
        "polkitd",
        "upowerd",
        "thermald",
        "colord",
        "accounts-daemon",
        "rtkit-daemon",
        "dconf-service",
        "gdm-x-session",
        "gdm-session-worker",
        "gdm",
        "lightdm",
        "sddm",
        "Xorg",
        "Xwayland",
    ]
    SETTINGS_GROUP = "SigGUI"
    LAST_SELECTION_KEY = "last_selected_process"
    PREVIOUS_SELECTIONS_KEY = "previous_selections"
    LAST_SIGNAL_KEY = "last_signal"
    ICONS = {
        psutil.STATUS_STOPPED: QStyle.StandardPixmap.SP_MediaPause,
        psutil.STATUS_RUNNING: QStyle.StandardPixmap.SP_MediaPlay,
        psutil.STATUS_DEAD: QStyle.StandardPixmap.SP_TrashIcon,
        psutil.STATUS_IDLE: QStyle.StandardPixmap.SP_MediaPlay,
        psutil.STATUS_LOCKED: QStyle.StandardPixmap.SP_VistaShield,
        psutil.STATUS_ZOMBIE: QStyle.StandardPixmap.SP_MessageBoxWarning,
        psutil.STATUS_SLEEPING: QStyle.StandardPixmap.SP_DialogSaveButton,
        psutil.STATUS_PARKED: QStyle.StandardPixmap.SP_MessageBoxWarning,
        psutil.STATUS_WAKING: QStyle.StandardPixmap.SP_MediaPlay,
        psutil.STATUS_WAITING: QStyle.StandardPixmap.SP_MediaPlay,
        psutil.STATUS_DISK_SLEEP: QStyle.StandardPixmap.SP_DialogSaveButton,
        psutil.STATUS_TRACING_STOP: QStyle.StandardPixmap.SP_MediaPause,
    }

    def __init__(self):
        """Initialize SigGUI."""
        super().__init__()
        self.setWindowTitle("Sig Script GUI")
        self.setGeometry(100, 100, 600, 500)

        self.settings = QSettings("SigGUIApp", "SigGUI")

        self.process_list_widget = QListWidget()
        self.process_list_widget.setContextMenuPolicy(
            Qt.ContextMenuPolicy.CustomContextMenu
        )
        self.process_list_widget.customContextMenuRequested.connect(
            self.show_process_context_menu
        )

        self.signal_combo = QComboBox()
        self.signal_combo.addItems(
            ["toggle", "stop", "cont", "KILL", "TERM", "HUP", "INT", "QUIT", "USR1", "USR2"]
        )

        last_signal = self.settings.value(
            self.SETTINGS_GROUP + "/" + self.LAST_SIGNAL_KEY, "toggle", type=str
        )
        self.signal_combo.setCurrentText(last_signal)

        self.pick_button = QPushButton("Pick Process")
        self.pick_button.setIcon(
            QApplication.style().standardIcon(QStyle.StandardPixmap.SP_ArrowUp)
        )
        self.run_button = QPushButton("Run Sig")
        self.run_button.setIcon(
            QApplication.style().standardIcon(QStyle.StandardPixmap.SP_CommandLink)
        )
        self.output_text = QTextEdit()
        self.output_text.setReadOnly(True)
        self.output_checkbox = QCheckBox("Show Output")
        self.output_checkbox.setChecked(False)
        self.output_text.setVisible(False)
        self.output_label = QLabel("Output:")
        self.output_label.setVisible(False)
        self.search_bar = QLineEdit()
        self.search_bar.setPlaceholderText("Search processes...")

        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)

        self.process = QProcess(self)
        self.process.readyReadStandardOutput.connect(self.read_output)
        self.process.readyReadStandardError.connect(self.read_error)
        self.process.finished.connect(self.process_finished)

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.populate_process_list)
        self.timer.start(5000)

        self.pick_button.clicked.connect(self.pick_process)
        self.run_button.clicked.connect(self.run_sig)
        self.search_bar.textChanged.connect(self.filter_process_list)
        self.search_bar.returnPressed.connect(self.on_search_return_pressed)
        self.process_list_widget.itemDoubleClicked.connect(
            self.on_process_double_clicked
        )
        self.output_checkbox.stateChanged.connect(self.toggle_output_visibility)

        self.processes = []
        self.previous_selections = self.load_previous_selections()
        self.populate_process_list()
        self.load_last_selection()
        self.close_on_finish = False

        # Layout setup
        signal_layout = QHBoxLayout()
        signal_layout.addWidget(QLabel("Signal:"))
        signal_layout.addWidget(self.signal_combo)
        signal_layout.addWidget(self.pick_button)
        signal_layout.addWidget(self.run_button)

        search_layout = QHBoxLayout()
        search_layout.addWidget(QLabel("Search:"))
        search_layout.addWidget(self.search_bar)

        main_layout = QVBoxLayout()
        main_layout.addLayout(signal_layout)
        main_layout.addLayout(search_layout)
        main_layout.addWidget(QLabel("Processes:"))
        main_layout.addWidget(self.process_list_widget)
        main_layout.addWidget(self.output_label)
        main_layout.addWidget(self.output_text)

        central_widget = QWidget()
        central_widget.setLayout(main_layout)
        self.setCentralWidget(central_widget)

        # Menu Bar - File Menu
        menubar = self.menuBar()
        file_menu = menubar.addMenu("&File")

        # About Action
        about_action = file_menu.addAction("About")
        about_action.triggered.connect(self.show_about_dialog)

        # Quit Action
        quit_action = file_menu.addAction(
            QApplication.style().standardIcon(QStyle.StandardPixmap.SP_DialogCloseButton),
            "Quit",
        )
        quit_action.triggered.connect(self.close)

        # Set Tab Order for Keyboard Navigation
        self.setTabOrder(self.search_bar, self.process_list_widget)
        self.setTabOrder(self.process_list_widget, self.signal_combo)
        self.setTabOrder(self.signal_combo, self.pick_button)
        self.setTabOrder(self.pick_button, self.run_button)
        self.setTabOrder(self.run_button, self.search_bar)

    def populate_process_list(self, search=None):
        """Populate process list, filter useless, sort by previous selections."""
        if search is None:
            search = self.search_bar.text()

        self.process_list_widget.clear()
        current_user_uid = os.getuid()
        processes = psutil.process_iter(
            ["pid", "name", "uids", "ppid", "status", "terminal"]
        )
        process_names_set = set()
        self.processes = []  # clear the processes

        for proc in processes:
            try:
                process_info = proc.info
                process_name = process_info["name"]
                ppid = process_info["ppid"]
                tty = process_info["terminal"]
                status = process_info["status"]
                pid = process_info["pid"]
                user = proc.username()

                if search.lower() not in process_name.lower():
                    continue

                is_daemon = (ppid == 1 or ppid == 0) and tty is None

                if (
                    process_info["uids"].real == current_user_uid
                    and process_name not in self.USELESS_PROCESS_NAMES
                    and not any(
                        prefix in process_name for prefix in ["gsd-", "gnome-session-"]
                    )
                    and not is_daemon
                ):
                    if process_name not in process_names_set:
                        process_names_set.add(process_name)

                        item_text = (
                            f"{process_name}      (PID: {pid}, User: {user}, Status: {status})"
                        )
                        item = QListWidgetItem(item_text)
                        if status in self.ICONS:
                            icon = QApplication.style().standardIcon(self.ICONS[status])
                            item.setIcon(icon)
                        self.processes.append({
                            'name': process_name,
                            'text': item_text,
                            'item': item
                        })

            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                pass
            except Exception as e:
                print(f"Error getting process info for {proc.pid} ({proc.info["name"]}): {e}")

        def sort_key(proc):
            try:
                return self.previous_selections.index(proc['name'])
            except ValueError:
                return len(self.previous_selections) + 1

        self.processes.sort(key=sort_key)
        for proc in self.processes:
            self.process_list_widget.addItem(proc['item'])
        self.status_bar.showMessage("Process list refreshed.", 3000)

    def filter_process_list(self, search_text):
        """Filter process list by search text, maintain sorting and uniqueness."""
        self.populate_process_list(search_text)

    def toggle_process_selection(self, item):
        """Toggle selection state of process list item."""
        item.setSelected(not item.isSelected())

    def pick_process(self):
        """Pick a process name from KWin and select it in the list."""
        try:
            print("Running qdbus6 command...")
            result = subprocess.run(
                ["qdbus6", "org.kde.KWin", "/KWin", "queryWindowInfo"],
                capture_output=True,
                text=True,
                check=True,
            )
            output = result.stdout
            print("qdbus6 Output:", output)
            resource_name_line = next(
                (line for line in output.splitlines() if "resourceName" in line), None
            )
            print("Resource Name Line:", resource_name_line)
            if resource_name_line:
                process_name = resource_name_line.split()[1]
                self.search_bar.setText(process_name)
                self.output_text.append(f"Picked process name from KWin: {process_name}")
                self.select_process_in_list(process_name)
            else:
                self.output_text.append("Could not extract process name from KWin output.")
        except FileNotFoundError as e:
            print(f"FileNotFoundError: {e}")
            QMessageBox.warning(self, "Error", "qdbus6 not found. Is KDE KWin running?")
        except subprocess.CalledProcessError as e:
            print(f"CalledProcessError: {e}")
            QMessageBox.warning(
                self, "Error", f"Error running qdbus6: {e.stderr or e.stdout or str(e)}"
            )
        except Exception as e:
            print(f"Exception: {e}")
            QMessageBox.critical(
                self, "Error", f"Unexpected error during pick process: {e}"
            )

    def select_process_in_list(self, process_name):
        """Select process in list widget by name."""
        for index in range(self.process_list_widget.count()):
            item = self.process_list_widget.item(index)
            if item.text().startswith(f"{process_name} (PID:"):
                item.setSelected(True)
                return

    def run_sig(self):
        """Run sig bash script with selected signal and processes."""
        selected_signal = self.signal_combo.currentText()
        self.settings.setValue(
            self.SETTINGS_GROUP + "/" + self.LAST_SIGNAL_KEY, selected_signal
        )
        selected_processes = [
            item.text().split(" ")[0]
            for item in self.process_list_widget.selectedItems()
        ]

        if selected_processes:
            self.save_last_selection(selected_processes[0])
            self.update_previous_selections(selected_processes[0])
        else:
            self.save_last_selection("")

        if not selected_processes:
            QMessageBox.warning(self, "Warning", "No processes selected.")
            return

        command = ["sig", selected_signal] + selected_processes

        self.output_label.setVisible(True)
        self.output_text.clear()
        self.output_text.setVisible(True)
        self.output_checkbox.setChecked(True)
        self.status_bar.showMessage(f"Running command: {' '.join(command)}", 5000)
        self.output_text.append(f"Running command: {' '.join(command)}")
        self.process.start(command[0], command[1:])

    def read_output(self):
        """Read and display standard output from QProcess, removing newlines."""
        output = self.process.readAllStandardOutput().data().decode()
        for line in output.splitlines():
            cleaned_line = line.strip()
            if cleaned_line:
                self.output_text.append(cleaned_line)

    def read_error(self):
        """Read and display standard error from QProcess in red, removing newlines."""
        error = self.process.readAllStandardError().data().decode()
        for line in error.splitlines():
            cleaned_line = line.strip()
            if cleaned_line:
                self.output_text.append(f"<span style='color: red;'>{cleaned_line}</span>")

    def process_finished(self, exit_code, exit_status):
        """Handle process finished signal, display status, close if needed."""
        if exit_status == QProcess.ExitStatus.NormalExit:
            self.output_text.append(f"Process finished with exit code: {exit_code}")
            self.status_bar.showMessage(
                f"Process finished with exit code: {exit_code}", 5000
            )
        else:
            self.output_text.append(
                f"<span style='color: red;'>Process crashed with exit code: {exit_code}</span>"
            )
            self.status_bar.showMessage(
                f"Process crashed with exit code: {exit_code}", 5000
            )

        if self.close_on_finish:
            self.close()

    def save_last_selection(self, process_name):
        """Save last selected process name to QSettings."""
        settings = self.settings
        settings.beginGroup(self.SETTINGS_GROUP)
        settings.setValue(self.LAST_SELECTION_KEY, process_name)
        settings.endGroup()

    def load_last_selection(self):
        """Load last selected process name from QSettings and restore."""
        settings = self.settings
        settings.beginGroup(self.SETTINGS_GROUP)
        last_selected_process = settings.value(
            self.LAST_SELECTION_KEY, "", type=str
        )
        settings.endGroup()

        if last_selected_process:
            self.search_bar.setText(last_selected_process)
            self.restore_selection(last_selected_process)
            self.filter_process_list(last_selected_process)

    def restore_selection(self, process_name):
        """Restore selection in process list by name."""
        self.select_process_in_list(process_name)

    def on_search_return_pressed(self):
        """Handle Enter key press to run toggle command."""
        if self.process_list_widget.count() > 0:
            if not self.process_list_widget.selectedItems():
                self.process_list_widget.item(0).setSelected(True)
            process_name = self.process_list_widget.selectedItems()[0].text().split(
                " "
            )[0]
        else:
            process_name = self.search_bar.text()

        if not process_name:
            QMessageBox.warning(self, "Warning", "No process selected or entered.")
            return

        self._run_toggle_command()

    def toggle_output_visibility(self, state):
        """Toggle visibility of output text area and label."""
        self.output_text.setVisible(state)
        self.output_label.setVisible(state)

    def load_previous_selections(self):
        """Load previous selections list from QSettings."""
        self.settings.beginGroup(self.SETTINGS_GROUP)
        previous_selections = self.settings.value(
            self.PREVIOUS_SELECTIONS_KEY, [], type=list
        )
        self.settings.endGroup()
        return previous_selections

    def save_previous_selections(self):
        """Save previous selections list to QSettings."""
        self.settings.beginGroup(self.SETTINGS_GROUP)
        self.settings.setValue(self.PREVIOUS_SELECTIONS_KEY, self.previous_selections)
        self.settings.endGroup()

    def update_previous_selections(self, process_name):
        """Update previous selections list, ensuring uniqueness and order."""
        if process_name and process_name not in self.USELESS_PROCESS_NAMES:
            if process_name in self.previous_selections:
                self.previous_selections.remove(process_name)
            self.previous_selections.insert(0, process_name)
            self.previous_selections = self.previous_selections[:5]
            self.save_previous_selections()

    def keyPressEvent(self, event):
        """Override key press event to handle Enter and Esc keys."""
        if event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            self.on_search_return_pressed()
        elif event.key() == Qt.Key.Key_Escape:
            self.close()
        elif event.key() == Qt.Key.Key_Space:
            if self.process_list_widget.hasFocus():
                items = self.process_list_widget.selectedItems()
                if items:
                    self.toggle_process_selection(items[0])

        super().keyPressEvent(event)

    def on_process_double_clicked(self, item):
        """Handle double click on process item to run toggle command."""
        self._run_toggle_command()

    def _run_toggle_command(self):
        """Central function to run toggle command and close GUI."""
        if not self.process_list_widget.selectedItems() and self.process_list_widget.count() > 0:
            self.process_list_widget.item(0).setSelected(True)
        self.signal_combo.setCurrentText("toggle")
        self.close_on_finish = True
        self.run_sig()

    def show_about_dialog(self):
        """Show the About dialog."""
        QMessageBox.about(
            self,
            "About SigGUI",
            "SigGUI Application\n\n"
            "Version 1.0\n"
            "A PyQt6 GUI for sending signals to processes.\n\n"
            "Created by moisrex",
        )

    def show_process_details_dialog(self, process_name_with_details):
        """Show process details in a dialog."""
        pid = None

        if "PID:" in process_name_with_details:
            try:
                pid_str_parts = process_name_with_details.split("PID: ")
                if len(pid_str_parts) < 2:
                    QMessageBox.warning(
                        self,
                        "Error",
                        f"Could not fetch details for process '{process_name_with_details}': Invalid process name format.",
                    )
                    return

                pid_str_with_rest = pid_str_parts[1]
                pid_str = pid_str_with_rest.split(",")[0]
                pid = int(pid_str)
            except (ValueError, IndexError):
                QMessageBox.warning(
                    self,
                    "Error",
                    f"Could not fetch details for process '{process_name_with_details}': Error parsing PID.",
                )
                return
        else:
            found_processes = []
            for proc in psutil.process_iter(["pid", "name"]):
                if proc.info["name"] == process_name_with_details:
                    found_processes.append(proc)

            if not found_processes:
                QMessageBox.warning(
                    self,
                    "Error",
                    f"Could not fetch details for process '{process_name_with_details}': Process not found.",
                )
                return
            elif len(found_processes) > 1:
                QMessageBox.warning(
                    self,
                    "Warning",
                    f"Multiple processes found with name '{process_name_with_details}'. Showing details for the first one.",
                )
            pid = found_processes[0].info["pid"]

        try:
            if pid is not None:
                proc = psutil.Process(pid)

                details_text = f"Name: {proc.name()}\n"  # Use proc.name()
                details_text += f"PID: {proc.pid}\n"
                details_text += f"User: {proc.username()}\n"
                details_text += f"Status: {proc.status()}\n" # Use proc.status()
                details_text += f"CPU %: {proc.cpu_percent()}%\n"
                details_text += f"Memory %: {proc.memory_percent():.2f}%\n"
                details_text += f"Command: {' '.join(proc.cmdline())}\n"

                QMessageBox.information(
                    self,
                    f"Details for {process_name_with_details}",
                    details_text,
                    QMessageBox.StandardButton.Ok,
                )
            else:
                 QMessageBox.warning(
                    self,
                    "Error",
                    f"Could not fetch details for process '{process_name_with_details}': PID not found.",
                )


        except psutil.NoSuchProcess:
            QMessageBox.warning(
                self,
                "Error",
                f"Could not fetch details for process '{process_name_with_details}': No such process (PID: {pid}).",
            )
        except Exception as e:
            QMessageBox.warning(
                self,
                "Error",
                f"Could not fetch details for process '{process_name_with_details}': {e}",
            )

    def show_process_context_menu(self, position):
        """Show context menu when right-clicking on a process in the list."""
        menu = QMenu(self)
        stop_action = menu.addAction(
            QApplication.style().standardIcon(QStyle.StandardPixmap.SP_MediaStop), "Stop"
        )
        continue_action = menu.addAction(
            QApplication.style().standardIcon(QStyle.StandardPixmap.SP_MediaPlay),
            "Continue",
        )
        kill_action = menu.addAction(
            QApplication.style().standardIcon(QStyle.StandardPixmap.SP_DialogNoButton),
            "Kill",
        )
        details_action = menu.addAction("Details")

        selected_item = self.process_list_widget.itemAt(position)
        if selected_item:
            process_name_with_details = selected_item.text()
            process_name = selected_item.text().split(" ")[0]

            stop_action.triggered.connect(
                lambda: self.send_signal_to_process("stop", process_name)
            )
            continue_action.triggered.connect(
                lambda: self.send_signal_to_process("cont", process_name)
            )
            kill_action.triggered.connect(
                lambda: self.confirm_kill_process(process_name)
            )
            details_action.triggered.connect(
                lambda: self.show_process_details_dialog(process_name_with_details)
            )

            menu.exec(self.process_list_widget.viewport().mapToGlobal(position))

    def send_signal_to_process(self, signal_name, process_name):
        """Helper function to send a signal to a process."""
        self.signal_combo.setCurrentText(signal_name.lower())
        self.search_bar.setText(process_name)
        self._run_toggle_command()

    def confirm_kill_process(self, process_name):
        """Show confirmation dialog before killing a process."""
        reply = QMessageBox.question(
            self,
            "Confirm Kill",
            f"Are you sure you want to KILL process '{process_name}'?\nThis is a forceful termination.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )

        if reply == QMessageBox.StandardButton.Yes:
            self.send_signal_to_process("KILL", process_name)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    gui = SigGUI()
    gui.show()
    gui.search_bar.setFocus()
    gui.search_bar.selectAll()
    sys.exit(app.exec())
