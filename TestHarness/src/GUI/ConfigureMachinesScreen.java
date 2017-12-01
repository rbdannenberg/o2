package GUI;


import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Component;
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
import javax.swing.DefaultCellEditor;
import javax.swing.JButton;
import javax.swing.JCheckBox;
import javax.swing.JFrame;
import javax.swing.JLabel;
import javax.swing.JOptionPane;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JTable;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import javax.swing.SwingUtilities;
import javax.swing.border.EmptyBorder;
import javax.swing.event.TableModelEvent;
import javax.swing.event.TableModelListener;
import javax.swing.table.DefaultTableCellRenderer;
import javax.swing.table.DefaultTableModel;

@SuppressWarnings("serial")
public class ConfigureMachinesScreen extends JPanel{
	static JTable table = null;
	static JFrame frame = null;
	JTextArea output;
        String masterPwd;
	Map<Integer, StringBuilder> selectedMap = new HashMap<Integer, StringBuilder>();
	private Set<String> contents = new HashSet();
         JTextField text;
                
	public ConfigureMachinesScreen() {                
            //System.out.println("In initializer .. ");
            initializePanel();
	}

	private void initializePanel() {
                //System.out.println("In initcomp initipanelssss");
		setLayout(new BorderLayout());
		setPreferredSize(new Dimension(1040, 560));
		setName("Test Harness - Configure Machines");
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
                        try {
                            ConfigureButtonactionPerformed(evt);
                        } catch (IOException ex) {
                            Logger.getLogger(ConfigureMachinesScreen.class.getName()).log(Level.SEVERE, null, ex);
                        }
                    }
                });
                
                
		JButton BackButton = new JButton("Back");
                BackButton.addActionListener(new java.awt.event.ActionListener() {
                    public void actionPerformed(java.awt.event.ActionEvent evt) {
                            BackButtonactionPerformed(evt);
                    }
                });
                JLabel label = new JLabel("Version #");
                text = new JTextField();
                text.setColumns(20);
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
                command.add(label);
		command.add(text);
		command.add(configureButton);
		command.add(BackButton);
                

		add(pane, BorderLayout.CENTER);
		add(command, BorderLayout.SOUTH);

		// MyTableModel NewModel = new MyTableModel();
		// this.table.setModel(model);

	}

	
	public void ConfigureButtonactionPerformed(ActionEvent e) throws IOException {
		ArrayList<String> machineList = new ArrayList<String>();
                
		if(selectedMap.keySet().size() == 0){
                    int result = JOptionPane.showConfirmDialog(null, "You can use only the local machine for testing since you have not made any selection. Press OK to continue or cancel to change your selection", "Machine Selection", JOptionPane.OK_CANCEL_OPTION);
                    if(result == 0)
                    {
                         for (int key : selectedMap.keySet()) {
			machineList.add(selectedMap.get(key).toString());
                      }
                        String hashValue = text.getText();
                        if(hashValue.equals("") || hashValue.equals(null))
                        {
                            JOptionPane.showMessageDialog(null, "Please enter a build version # to continue!");
                            return;
                        }
                        this.setEnabled(false);
                        frame.setVisible(false);
                        CreateTestSuiteScreen nextScreen = new CreateTestSuiteScreen(machineList, hashValue);
                        nextScreen.setVisible(true); 
                    } 
                    
                }
                else
                {
                     for (int key : selectedMap.keySet()) {
			machineList.add(selectedMap.get(key).toString());
                      }
                        String hashValue = text.getText();
                        if(hashValue.equals("") || hashValue.equals(null))
                        {
                            JOptionPane.showMessageDialog(null, "Please enter a build version # to continue!");
                            return;
                        }
                        this.setEnabled(false);
                        frame.setVisible(false);
                        CreateTestSuiteScreen nextScreen = new CreateTestSuiteScreen(machineList, hashValue);
                        nextScreen.setVisible(true); 
                }
	
	}
        
        public void BackButtonactionPerformed(ActionEvent e) {
		this.setEnabled(false);
		frame.setVisible(false);
		new SelectTestSuiteScreen().setVisible(true);
	}

	public static Object[][] ReadFile(File DataFile) {
		//System.out.println("In read csv file .. ");
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
        
     
    
    private static JTable getNewRenderedTable(final JTable table, final ArrayList<String> machineList) {
        System.out.println("The table in machines conf screen is getting rendered .. ");
       
        table.setDefaultRenderer(Object.class, new DefaultTableCellRenderer(){
            @Override
            public Component getTableCellRendererComponent(JTable table,
                    Object value, boolean isSelected, boolean hasFocus, int row, int col) {
                
                super.getTableCellRendererComponent(table, value, isSelected, hasFocus, row, col);
                StringBuilder thisRow = new StringBuilder();
                for (int j = 1; j < table.getColumnCount(); j++) {
			thisRow.append((String) table.getModel().getValueAt(row, j) + " ");
		}
                //System.out.println("Value now : " + value);
                for(String machine : machineList) {
                    if(machine.equals(thisRow.toString())){
                            System.out.println("This machine matches : " + machine);
                            //setValue(true);      
                            //boolean selected = true;
                            //JCheckBox jCheckBox = new javax.swing.JCheckBox();
                            //jCheckBox.setSelected(true);
                            //table.getColumnModel().getColumn(0).setCellEditor(new DefaultCellEditor(jCheckBox));
                            //DefaultTableModel myModel = (DefaultTableModel)table.getModel();
                            //myModel.setValueAt(jCheckBox, row, 0);
                            //table.setModel(myModel);
                            //System.out.println(table.isRowSelected(row));
                            //table.setRowSelectionInterval(0, table.getColumnCount());
                            //table.getSelectionModel().addSelectionInterval(0, table.getColumnCount());
                            System.out.println("Value at the first column: " + table.getModel().getValueAt(row, 0));
                            //setValueAt("Dummmy", row, 1);
                                                       
                            if(col == 1){
                                setValue(true); // what is the function to change the value of a check box?
                                System.out.println("Changing the text in the first column .. ");
                            }
                            if(col == 0){
                                // the program is not at all entering this condition.. 
                                System.out.println("Now changing the state of the checkbox .. ");
                                setValue(Boolean.TRUE);
                                setEnabled(true);
                                
                                System.out.println("Value: " + value + "\n this: " + this);
                            }
                    }
                }
                return this;
            }   
        });
        return table;  
    }

	public static void showFrame() {
		//System.out.println("In show frame .. ");
                JPanel panel = new ConfigureMachinesScreen();
		panel.setOpaque(true);

		frame = new JFrame("Test Harness - Configure machines");
		frame.setDefaultCloseOperation(JFrame.DISPOSE_ON_CLOSE);
		frame.setContentPane(panel);
		frame.pack();
		frame.setVisible(true);
	}
        
        public static void RenderTable(ArrayList<String> machines){
            table = getNewRenderedTable(table, machines); 
        }

	public static void main(String[] args) {
		SwingUtilities.invokeLater(new Runnable() {
			public void run() {
                                //int bypass = 0;
                                //ArrayList<String> machines = new ArrayList<String>();
				ConfigureMachinesScreen.showFrame();
                                //System.out.println("In main of conf mach screen .. ");
			}
		});
	}
}
