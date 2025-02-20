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
    QPushButton,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

class SigGUI(QMainWindow):
    """Main window for the Sig Script GUI application."""

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

    def __init__(self):
        """Initialize the SigGUI."""
        super().__init__()
        self.setWindowTitle("Sig Script GUI")
        self.setGeometry(100, 100, 800, 600)

        self.settings = QSettings("SigGUIApp", "SigGUI")

        self.process_list_widget = QListWidget()
        self.signal_combo = QComboBox()
        self.signal_combo.addItems(
            ["toggle", "stop", "cont", "KILL", "TERM", "HUP", "INT", "QUIT", "USR1", "USR2"]
        )
        self.pick_button = QPushButton("Pick Process (KWin)")
        self.run_button = QPushButton("Run Sig")
        self.output_text = QTextEdit()
        self.output_text.setReadOnly(True)
        self.output_checkbox = QCheckBox("Show Output")
        self.output_checkbox.setChecked(False)
        self.output_text.setVisible(False)
        self.output_label = QLabel("Output:")
        self.output_label.setVisible(False)
        self.refresh_button = QPushButton("Refresh Process List")
        self.search_bar = QLineEdit()
        self.search_bar.setPlaceholderText("Search processes...")

        self.process = QProcess(self)
        self.process.readyReadStandardOutput.connect(self.read_output)
        self.process.readyReadStandardError.connect(self.read_error)
        self.process.finished.connect(self.process_finished)

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.populate_process_list)

        self.pick_button.clicked.connect(self.pick_process)
        self.run_button.clicked.connect(self.run_sig)
        self.refresh_button.clicked.connect(self.populate_process_list)
        self.search_bar.textChanged.connect(self.filter_process_list)
        self.search_bar.returnPressed.connect(self.on_search_return_pressed)
        self.process_list_widget.itemDoubleClicked.connect(self.on_process_double_clicked)
        self.output_checkbox.stateChanged.connect(self.toggle_output_visibility)

        self.all_process_names = []
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
        signal_layout.addWidget(self.refresh_button)

        search_layout = QHBoxLayout()
        search_layout.addWidget(QLabel("Search:"))
        search_layout.addWidget(self.search_bar)

        output_toggle_layout = QHBoxLayout()
        output_toggle_layout.addWidget(self.output_checkbox)

        main_layout = QVBoxLayout()
        main_layout.addLayout(signal_layout)
        main_layout.addLayout(search_layout)
        main_layout.addWidget(QLabel("Processes:"))
        main_layout.addWidget(self.process_list_widget)
        main_layout.addLayout(output_toggle_layout)
        main_layout.addWidget(self.output_label)
        main_layout.addWidget(self.output_text)

        central_widget = QWidget()
        central_widget.setLayout(main_layout)
        self.setCentralWidget(central_widget)

    def populate_process_list(self):
        """Populate process list, filter useless, sort by previous selections."""
        self.process_list_widget.clear()
        current_user_uid = os.getuid()
        processes = psutil.process_iter(["pid", "name", "uids", "ppid", "status", "terminal"]) # Corrected attributes - 'sid' removed
        process_names_set = set()
        process_names_list = []

        for proc in processes:
            try:
                process_info = proc.info
                process_name = process_info["name"]
                ppid = process_info["ppid"]
                tty = process_info["terminal"]

                # Simplified daemon check: PPID 1 or 0 and no controlling TTY
                is_daemon = (ppid == 1 or ppid == 0) and tty is None

                if (
                    process_info["uids"].real == current_user_uid
                    and process_name not in self.USELESS_PROCESS_NAMES
                    and not any(prefix in process_name for prefix in ["gsd-", "gnome-session-"])
                    and not is_daemon
                ):
                    if process_name not in process_names_set:
                        process_names_set.add(process_name)
                        process_names_list.append(process_name)
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                pass

        def sort_key(process_name):
            try:
                return self.previous_selections.index(process_name)
            except ValueError:
                return len(self.previous_selections) + 1

        process_names_list.sort(key=sort_key)

        self.all_process_names = process_names_list
        self.process_list_widget.addItems(self.all_process_names)

        self.filter_process_list(self.search_bar.text())

    def filter_process_list(self, search_text):
        """Filter process list by search text, maintain sorting and uniqueness."""
        self.process_list_widget.clear()
        process_names_to_display_set = set()
        process_names_to_display_list = []

        if not search_text:
            process_names_source = self.all_process_names
        else:
            process_names_source = [
                name
                for name in self.all_process_names
                if search_text.lower() in name.lower()
            ]

        for name in process_names_source:
            if name not in process_names_to_display_set:
                process_names_to_display_set.add(name)
                process_names_to_display_list.append(name)

        def sort_key(process_name):
            try:
                return self.previous_selections.index(process_name)
            except ValueError:
                return len(self.previous_selections) + 1

        process_names_to_display_list.sort(key=sort_key)
        self.process_list_widget.addItems(process_names_to_display_list)

    def toggle_process_selection(self, item):
        """Toggle selection state of process list item."""
        item.setSelected(not item.isSelected())

    def pick_process(self):
        """Pick a process name from KWin and select it in the list."""
        try:
            print("Running qdbus6 command...") # Debug print
            result = subprocess.run(
                ["qdbus6", "org.kde.KWin", "/KWin", "queryWindowInfo"],
                capture_output=True,
                text=True,
                check=True,
            )
            output = result.stdout
            print("qdbus6 Output:", output) # Debug print
            resource_name_line = next(
                (line for line in output.splitlines() if "resourceName" in line), None
            )
            print("Resource Name Line:", resource_name_line) # Debug print
            if resource_name_line:
                process_name = resource_name_line.split()[1]
                self.output_text.append(f"Picked process name from KWin: {process_name}")
                self.search_bar.setText(process_name) # <----- ADDED THIS LINE: Fill search bar
                self.select_process_in_list(process_name)
            else:
                self.output_text.append("Could not extract process name from KWin output.")
        except FileNotFoundError as e:
            print(f"FileNotFoundError: {e}") # Debug print
            QMessageBox.warning(self, "Error", "qdbus6 not found. Is KDE KWin running?")
        except subprocess.CalledProcessError as e:
            print(f"CalledProcessError: {e}") # Debug print
            QMessageBox.warning(
                self, "Error", f"Error running qdbus6: {e.stderr or e.stdout or str(e)}"
            )
        except Exception as e:
            print(f"Exception: {e}") # Debug print
            QMessageBox.critical(
                self, "Error", f"Unexpected error during pick process: {e}"
            )

    def select_process_in_list(self, process_name):
        """Select process in list widget by name."""
        for index in range(self.process_list_widget.count()):
            item = self.process_list_widget.item(index)
            if item.text() == process_name:
                item.setSelected(True)
                return

    def run_sig(self):
        """Run sig bash script with selected signal and processes."""
        selected_signal = self.signal_combo.currentText()
        selected_processes = [
            item.text() for item in self.process_list_widget.selectedItems()
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
        else:
            self.output_text.append(
                f"<span style='color: red;'>Process crashed with exit code: {exit_code}</span>"
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
        last_selected_process = settings.value(self.LAST_SELECTION_KEY, "", type=str)
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
            process_name = self.process_list_widget.selectedItems()[0].text()
        else:
            process_name = self.search_bar.text()

        if not process_name:
            QMessageBox.warning(self, "Warning", "No process selected or entered.")
            return

        self._run_toggle_command() # Corrected to no argument call

    def toggle_output_visibility(self, state):
        """Toggle visibility of output text area and label."""
        self.output_text.setVisible(state) # Corrected attribute name
        self.output_label.setVisible(state)

    def load_previous_selections(self):
        """Load previous selections list from QSettings."""
        settings = self.settings
        settings.beginGroup(self.SETTINGS_GROUP)
        previous_selections = settings.value(self.PREVIOUS_SELECTIONS_KEY, [], type=list)
        settings.endGroup()
        return previous_selections

    def save_previous_selections(self):
        """Save previous selections list to QSettings."""
        settings = self.settings
        settings.beginGroup(self.SETTINGS_GROUP)
        settings.setValue(self.PREVIOUS_SELECTIONS_KEY, self.previous_selections)
        settings.endGroup()

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
        super().keyPressEvent(event)

    def on_process_double_clicked(self, item):
        """Handle double click on process item to run toggle command."""
        process_name = item.text()
        self._run_toggle_command() # Corrected to no argument call

    def _run_toggle_command(self): # Corrected to no argument definition
        """Central function to run toggle command and close GUI."""
        if (
            not self.process_list_widget.selectedItems()
            and self.process_list_widget.count() > 0
        ):
            self.process_list_widget.item(0).setSelected(True)
        if self.process_list_widget.selectedItems():
            process_name = self.process_list_widget.selectedItems()[0].text()
        else: # Fallback to search bar if no selection in list
            process_name = self.search_bar.text()

        self.signal_combo.setCurrentText("toggle")
        self.close_on_finish = True
        self.run_sig()


if __name__ == "__main__":
    app = QApplication(sys.argv)
    gui = SigGUI()
    gui.show()
    gui.search_bar.setFocus()
    gui.search_bar.selectAll()
    sys.exit(app.exec())
