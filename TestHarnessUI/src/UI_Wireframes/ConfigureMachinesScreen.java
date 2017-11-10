package UI_Wireframes;


import java.awt.BorderLayout;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;
import java.io.LineNumberReader;
import static java.lang.System.exit;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.logging.Level;
import java.util.logging.Logger;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JFrame;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.JTextArea;
import javax.swing.SwingUtilities;
import javax.swing.border.EmptyBorder;
import javax.swing.event.TableModelEvent;
import javax.swing.event.TableModelListener;
import javax.swing.table.DefaultTableModel;

@SuppressWarnings("serial")
public class ConfigureMachinesScreen extends JPanel{
	JTable table = null;
	static JFrame frame = null;
	JTextArea output;
        String masterPwd;
	Map<Integer, StringBuilder> selectedMap = new HashMap<Integer, StringBuilder>();
	private Set<String> contents = new HashSet();

	public ConfigureMachinesScreen() {                
            System.out.println("In initializer .. "); 
            initializePanel();
	}

	private void initializePanel() {
                System.out.println("In initcomp initipanelssss");
		setLayout(new BorderLayout());
		setPreferredSize(new Dimension(600, 600));
		setName("Configure Machines");
		Object[] columnNames = { "Select", "MachineIP", "Type", "Username",
				"Password" };
		File DataFile = new File("Inputs/MachineConfiguration.txt");
		Object[][] data = ReadFile(DataFile);
		// add a nice border
		setBorder(new EmptyBorder(5, 5, 5, 5));
                
                /*
                //masterPwd= JOptionPane.showInputDialog(this,"Please enter master machine password: ");
                int wrongAttempts = 3;
                while(!masterPwd.equals("admin")) {
                    if(wrongAttempts<=1) {
                        JOptionPane.showMessageDialog(null, "Authentication Failed");
                        exit(0);
                    }
                    masterPwd =JOptionPane.showInputDialog(this,"Incorrect password! \n\nYou have only "+ (wrongAttempts - 1) +" more attempt(s). \n\nPlease enter master machine password again: ");
                    wrongAttempts--;
                }*/
                    
               // System.out.println("Master password.."+masterPwd);
		DefaultTableModel model = new DefaultTableModel(data, columnNames);
		this.table = new JTable(model) {
			private static final long serialVersionUID = 1L;
			@Override
			public Class getColumnClass(int column) {
				switch (column) {
				case 0:
					return Boolean.class;
				case 1:
					return String.class;
				case 2:
					return String.class;
				case 3:
					return String.class;
				case 4:
					return String.class;
				default:
					return Boolean.class;
				}
			}
		};
                
		table.setFillsViewportHeight(true);
		JScrollPane pane = new JScrollPane(table);
                
		JButton configureButton = new JButton("Configure");
		
                
                configureButton.addActionListener(new java.awt.event.ActionListener() {
                    public void actionPerformed(java.awt.event.ActionEvent evt) {
                            ConfigureButtonactionPerformed(evt);
                    }
                });
                
                
		JButton BackButton = new JButton("Back");
                
		JCheckBox checkBox = new javax.swing.JCheckBox();
		table.setRowSelectionAllowed(true);
		table.setColumnSelectionAllowed(false);
		table.getModel().addTableModelListener(new TableModelListener() {
			String contents2 = new String();

			@Override
			public void tableChanged(TableModelEvent e) {
				StringBuilder rowSelected = new StringBuilder();

				if ((Boolean) table.getModel().getValueAt(
						table.getSelectedRow(), 0)) {
					//System.out.println((Boolean) table.getModel().getValueAt(table.getSelectedRow(), 0));

					for (int j = 1; j < table.getColumnCount(); j++) {
						rowSelected.append((String) table.getModel()
								.getValueAt(table.getSelectedRow(), j) + " ");
					}
					selectedMap.put(table.getSelectedRow(), rowSelected);
				} else {
					if (selectedMap.containsKey(table.getSelectedRow())) {
						//System.out.println("Key found and row unselected");
						selectedMap.remove(table.getSelectedRow());

					}

				}

				//System.out.println(selectedMap);
			}
		});

		JPanel command = new JPanel(new FlowLayout());
		output = new JTextArea(1, 10);
		command.add(configureButton);
		command.add(BackButton);
		//command.add(output);

		add(pane, BorderLayout.CENTER);
		add(command, BorderLayout.SOUTH);

		// MyTableModel NewModel = new MyTableModel();
		// this.table.setModel(model);

	}

	
	public void ConfigureButtonactionPerformed(ActionEvent e) {
		ArrayList<String> machineList = new ArrayList<String>();
		for (int key : selectedMap.keySet()) {
			machineList.add(selectedMap.get(key).toString());
		}
		this.setEnabled(false);
		frame.setVisible(false);
		CreateTestSuiteScreen nextScreen = new CreateTestSuiteScreen(machineList);
		nextScreen.setVisible(true);
	}

	public static Object[][] ReadFile(File DataFile) {
		System.out.println("In read csv file .. ");
                LineNumberReader lnr = null;
		int i = 0;
		try {
			lnr = new LineNumberReader(new FileReader(DataFile));
		} catch (FileNotFoundException e1) {
			e1.printStackTrace();
		}
		try {
			lnr.skip(Long.MAX_VALUE);
		} catch (IOException e1) {

			e1.printStackTrace();
		}
		
		try {
			lnr.close();
		} catch (IOException e1) {

			e1.printStackTrace();
		}
		Object[] OneRow = null;
		try {
			BufferedReader brd = new BufferedReader(new FileReader(DataFile));
                        String st;
			while ((st = brd.readLine()) != null) {				 
				OneRow = st.split(",|\\s|;");
				break;
			}
		} catch (Exception e) {
			String errmsg = e.getMessage();
			System.out.println("File not found..." + errmsg);
		}
		final Object[][] Rs = new Object[lnr.getLineNumber()][OneRow.length + 1];
		try {
			BufferedReader brd = new BufferedReader(new FileReader(DataFile));
                        String st;
			while ((st = brd.readLine()) != null) {		
				OneRow = st.split(",|\\s|;");
				Rs[i][0] = false;
				for (int j = 0; j < OneRow.length; j++) {
					Rs[i][j + 1] = OneRow[j];
				}
				i++;
			}
		} catch (Exception e) {
			String errmsg = e.getMessage();
			System.out.println("File not found:" + errmsg);
		}
//		for (int k = 0; k < Rs.length; k++) {
//			for (int l = 0; l < Rs[0].length; l++) {
//				System.out.print(Rs[k][l]);
//				System.out.print("\t");
//			}
//			System.out.println("\n");
//		}
		return Rs;
	}

	public static void showFrame() {
		System.out.println("In show frame .. ");
                JPanel panel = new ConfigureMachinesScreen();
		panel.setOpaque(true);

		frame = new JFrame("Machine Configuration");
		frame.setDefaultCloseOperation(JFrame.DISPOSE_ON_CLOSE);
		frame.setContentPane(panel);
		frame.pack();
		frame.setVisible(true);
	}

	public static void main(String[] args) {
		SwingUtilities.invokeLater(new Runnable() {
			public void run() {
                                //int bypass = 0;
				ConfigureMachinesScreen.showFrame();
                                System.out.println("In main of conf mach screen .. ");
			}
		});
	}
}
